# 视频背景

## 用户行为

设置 → 下落背景提供无背景、图片背景、视频背景三种模式。图片和视频分别保留路径，共用六种适配方式、不透明度、可读性遮罩和 256–4096 px 输出边长上限。只覆盖下落音符区。

音乐传输位置是权威时间。视频短于乐曲时循环，长于乐曲时截断；音乐变速时视频按相同倍率变化。暂停保留当前帧。自然结束保留音乐终点及对应视频帧，再次播放从零开始；显式停止仍归零。拖动期间音符和视频预览滑块目标，松开后提交一次音乐 seek，预览期间视频原声静音。

视频原声默认屏蔽。取消屏蔽后通过 Qt Multimedia 输出第一条音轨，跟随暂停、seek、倍率和循环；系统混音器将其与 FluidSynth 音频合并。视频画面始终由音乐时间控制。原声音频的独立设备时钟在 seek/循环时重锚，偏差超过 120 ms 时校正，不能宣称采样级音频同步。

## 实现边界

- BackgroundMode 与 schema 15：读取旧 enabled/path 配置为 Image/None；新模式优先，旧字段保留兼容。禁用不会删除文件路径。
- backgroundVideoPosition：纯整数微秒映射，内部循环按取余定位；终点取左极限，避免音乐恰好在循环边界结束时闪回首帧。导出尾音阶段保持同一终帧。
- VideoFrameReader：独立 FFmpeg 库解码器，单线程使用，顺序播放复用上下文，随机 seek 向关键帧定位后按 PTS 解码。保留当前帧和下一帧，不缓存完整视频；处理 B 帧、VFR 时间戳、非方形像素、旋转、解码尾部和取消。
- 视频轨缺少 duration 时进行可取消、恒定内存的 packet PTS 扫描，以末帧 PTS 加持续时间计算周期；不使用可能被音轨延长的容器总时长。此类素材首次打开需要额外读取文件。
- VideoBackgroundController：最多一个解码任务、一个可覆盖的最新请求；源代次与 seek 代次隔离过期结果，工作线程持有自身资源，不访问 GUI。快速拖动取消长 GOP 追赶。GUI 只接收已完成的 QImage。
- Qt 与 Vulkan 共用 placement、遮罩和 RGBA 图像。Vulkan 同尺寸帧复用按交换链图像隔离的纹理与 staging，内容变化不重建静态几何。
- 离屏导出拥有独立解码器，以输出帧换算后的音乐时间读取背景。共享 GPU 纹理更新前等待在途槽，保证正确性；视频背景可能降低原有三槽流水的并发收益。
- 原声导出先生成长度与视频周期一致的临时 PCM，循环、变速、裁到音乐结束，再补静音至尾音终点。开启乐曲音频时与原声归一化混音；关闭乐曲音频仍可单独导出已启用的视频原声。

## 支持范围

本地、可 seek、有限时长的 SDR 视频；常见容器 MP4/MKV/MOV/WebM/AVI，实际编码支持由随包部署的 FFmpeg 决定。输入限制每边不超过 8192、总像素不超过 4000 万；输出按设置缩小。不支持网络地址、直播和 HDR 自动色调映射，HDR 输入会明确报错。较长 GOP 的首次定位仍受解码性能限制；图片和视频原文件不复制进 dist。

默认静音时不创建 Qt 音频播放器。视频解码无需用户安装 FFmpeg 命令行程序；视频导出继续使用项目已有的外部 FFmpeg 配置。构建依赖指定 avcodec/avformat/swscale/swresample，未启用 GPL/nonfree 特性；发布脚本保留依赖版权文件。

## 验证与发布

video_background_timeline_and_decoder 覆盖循环边界、音乐终点、设置迁移、静音默认值、H.264 B 帧、正反 seek、EOF 后循环、尺寸上限、取消、快速拖动与失效回调，以及拖动释放后等待精确 seek 确认。存在外部 FFmpeg 时检查原声导出时长和尾段静音。

集成验收入口（需要可用的 Vulkan 设备和 PATH 中的 FFmpeg）：

~~~powershell
./build/windows-release/Release/midi_play_video_background_tests.exe --vulkan --audio --integration --window
~~~

- --vulkan：离屏动态帧、循环重复帧和结束定格；关闭无关的击键线动画后进行像素比较。
- --audio：真实 Qt 音频后端的加载、播放、暂停、变速、循环、终点停止与重播；测试设备音量设为零。
- --integration：完整 MP4 导出，分别验证静音、仅视频原声、原声与乐曲混音，含循环与尾音。
- --window：真实 Vulkan 窗口连续上传 24 个视频帧、循环像素一致性和后端切换。会短暂显示测试窗口。

测试素材 tests/fixtures/background-video.mp4 为 FFmpeg testsrc2 + 660 Hz 正弦波生成的 2 秒 320×180 / 24 fps H.264/AAC 文件，无第三方素材。生成命令：

~~~powershell
ffmpeg -f lavfi -i "testsrc2=size=320x180:rate=24:duration=2" -f lavfi -i "sine=frequency=660:sample_rate=44100:duration=2" -c:v libx264 -g 24 -bf 2 -pix_fmt yuv420p -c:a aac -shortest tests/fixtures/background-video.mp4
~~~

发布使用 scripts/Build-Windows.ps1。新增 --video-background-test CLI 检查在清理开发 SDK PATH 后加载已部署的解码库，读取首尾帧并反向 seek；该检查包含在 SOP 打包流程中。原声设备输出、4K 高码率视频和长 GOP 连续拖动需要在目标设备上人工验收。

回归素材 background-long-audio.mkv 为 2 秒画面与 5 秒音轨，用于验证缺少视频流 duration 时不按音轨长度循环：

~~~powershell
ffmpeg -f lavfi -i "testsrc2=size=96x64:rate=24:duration=2" -f lavfi -i "sine=frequency=660:sample_rate=44100:duration=5" -c:v libx264 -g 24 -bf 2 -pix_fmt yuv420p -c:a aac tests/fixtures/background-long-audio.mkv
~~~
