# 视频背景

## 用户行为

设置 → 下落背景提供无背景、图片背景、视频背景三种模式。图片和视频分别保留路径，共用六种适配方式、不透明度、可读性遮罩和 256–4096 px 输出边长上限。只覆盖下落音符区。

音乐传输位置是权威时间。视频短于乐曲时循环，长于乐曲时截断；音乐变速时视频按相同倍率变化。暂停保留当前帧。自然结束保留音乐终点及对应视频帧，再次播放从零开始；显式停止仍归零，自然结束或停态非零位置也可点击停止回到开头。拖动期间音符和视频预览滑块目标，松开后提交一次音乐 seek，预览期间视频原声静音。

视频原声默认屏蔽。取消屏蔽后通过 Qt Multimedia 输出第一条音轨，跟随暂停、seek、倍率和循环；系统混音器将其与 FluidSynth 音频合并。视频画面始终由音乐时间控制。原声音频的独立设备时钟在 seek/循环时重锚，偏差超过 120 ms 时校正，不能宣称采样级音频同步。

视频首帧为背景时间零点。原声与画面按源文件时间戳对齐：视频首帧之前的音频不播放，音轨较晚开始或先于视频结束时，对应区间静音；下一视频循环按相同规则恢复。播放状态由音乐时间和音轨有效区间决定，不依赖可能迟到的 EndOfMedia 事件。切换视频时隔离旧播放器的状态与错误回调。

## 实现边界

- BackgroundMode 与 schema 15：读取旧 enabled/path 配置为 Image/None；合法新模式优先（包括显式 None），缺失或非法值回退旧开关，非法值额外报告警告。旧字段保留兼容，禁用不会删除文件路径。
- backgroundVideoPosition：纯整数微秒映射，内部循环按取余定位；终点取左极限，避免音乐恰好在循环边界结束时闪回首帧。导出尾音阶段保持同一终帧。
- VideoFrameReader：独立 FFmpeg 库解码器，单线程使用，顺序播放复用上下文，随机 seek 向关键帧定位后按 PTS 解码。目标时间向下取整到流时间基，选择包含目标时刻的帧，避免粗时间基将目标舍入到未来帧。保留当前帧和下一帧，不缓存完整视频；处理 B 帧、VFR 时间戳、非方形像素、旋转、解码尾部和取消。
- 视频轨缺少 duration 时进行可取消、恒定内存的 packet PTS 扫描，以末帧 PTS 加持续时间计算周期；不使用可能被音轨延长的容器总时长。第一音轨缺少 duration 时也会扫描，以取得静音区间边界。当前元数据在默认静音时仍完整读取，两轨均缺少时长会各扫描一次，因此大文件首次打开可能有额外 I/O 延迟。
- VideoBackgroundController：最多一个解码任务、一个可覆盖的最新请求；拖动不反复取消和重建解码器，允许已完成的中间预览帧发布，再处理最新目标，停止拖动后收敛到最终目标。切换源才取消旧任务，并以源代次隔离过期结果。工作线程持有自身资源，不访问 GUI，GUI 只接收已完成的 QImage。
- Qt 与 Vulkan 共用 placement、遮罩和 RGBA 图像。Vulkan 同尺寸帧复用按交换链图像隔离的纹理与 staging，内容变化不重建静态几何。
- 离屏导出拥有独立解码器，以输出帧换算后的音乐时间读取背景。视频纹理、staging 和 descriptor 按三个帧槽隔离，仅在对应槽完成后复用，避免逐帧上传等待所有在途槽。静态图片继续共享纹理；共享 atlas 变化仍需等待。该优化消除背景上传的跨槽等待，但不保证总导出耗时同比改善。
- 原声导出先保留输入时间戳、减去视频轨起点并补齐音轨空缺，再生成长度与视频周期一致的临时 PCM，循环、变速、裁到音乐结束，最后补静音至尾音终点。开启乐曲音频时与原声归一化混音；关闭乐曲音频仍可单独导出已启用的视频原声。归一化混音与系统实时混音的响度可能不同。

## 支持范围

本地、可 seek、有限时长的 SDR 视频；常见容器 MP4/MKV/MOV/WebM/AVI，实际编码支持由随包部署的 FFmpeg 决定。输入限制每边不超过 8192、总像素不超过 4000 万；输出按设置缩小。不支持网络地址、直播和 HDR 自动色调映射，HDR 输入会明确报错。较长 GOP 的首次定位仍受解码性能限制；图片和视频原文件不复制进 dist。

默认静音时不创建 Qt 音频播放器。视频解码无需用户安装 FFmpeg 命令行程序；视频导出继续使用项目已有的外部 FFmpeg 配置。构建依赖指定 avcodec/avformat/swscale/swresample，未启用 GPL/nonfree 特性；发布脚本保留依赖版权文件。

## 验证与发布

CTest 注册以下分层测试，可使用 ctest --test-dir build/windows-release -C Release -L video-background --output-on-failure 单独运行：

| 测试 | 验证范围 |
| --- | --- |
| video_background_timeline_and_decoder | 循环边界、结束定格与显式停止、配置迁移、静音默认值、B 帧、粗时间基、视频轨独立时长、正反 seek、尺寸上限、取消、快速拖动、源失效回调与 seek 确认；无需外部 FFmpeg |
| video_background_audio_export | 原声导出精确时长、尾段静音、视频轨起点对齐及频率校验；需外部 FFmpeg |
| video_background_audio_device | 真实 Qt 音频后端的加载、播放、暂停、变速、循环、结束与重播、短音轨静音区间和迟到事件；需音频输出设备，测试音量为零 |
| video_background_vulkan | 离屏动态帧、循环与结束定格、三槽连续提交、异步输出与同步像素对照、背景上传无跨槽等待 |
| video_background_full_export | 完整 MP4 导出的静音、仅原声、原声与乐曲混音，包含循环和尾音；需 Vulkan 与外部 FFmpeg |
| video_background_window | 真实 Vulkan 窗口连续上传、循环像素一致性及后端切换；Windows 桌面上短暂显示测试窗口 |

集成测试带 integration 标签，缺少设备或工具时以返回码 77 标记跳过，不能计作验证通过。Vulkan 相关测试只在启用该后端时注册。常规 CTest 允许跳过；发布严格门禁会直接运行全部集成入口，任何非零退出码（包括 77）都会阻止打包。

集成验收入口（需要可用的 Vulkan 设备和 PATH 中的 FFmpeg）：

~~~powershell
./build/windows-release/Release/midi_play_video_background_tests.exe -platform windows --audio --audio-export --vulkan --integration --window
~~~

这些参数分别对应上述集成层；--integration 也包含原声导出验证。发布使用 scripts/Build-Windows.ps1，建议本功能的发行包启用严格门禁，并传入本机 FFmpeg 可执行文件的绝对路径：

~~~powershell
./scripts/Build-Windows.ps1 -RequireVideoIntegration -FfmpegTestPath 'C:/Tools/ffmpeg/bin/ffmpeg.exe'
~~~

脚本先执行全量 CTest 和严格集成验证，再暂存部署、清除开发 SDK PATH，运行依赖检查和 CLI 解码/渲染/MP3 冒烟测试，全部成功后替换正式包。--video-background-test 检查已部署解码库能读取首尾帧并反向 seek。build-info.json 的 videoIntegrationPassed 只有在严格门禁通过时为 true；false 表示未执行该门禁，不表示集成测试失败。外部 FFmpeg 测试工具与原始背景文件不复制进 dist。4K 高码率、长 GOP 持续拖动和实际扬声器听感仍需按目标设备验收。

## 回归素材

下列小型人工合成素材作为测试依赖保存在 Git 中，不属于应用默认背景，也不随发行包部署。以下命令在仓库根目录执行，需可用的 FFmpeg（含 libx264 编码器）。不同 FFmpeg 版本生成的编码字节可能不同，测试依赖的是时长、时间戳和内容特征。

测试素材 tests/fixtures/background-video.mp4 为 FFmpeg testsrc2 + 660 Hz 正弦波生成的 2 秒 320×180 / 24 fps H.264/AAC 文件，无第三方素材。生成命令：

~~~powershell
ffmpeg -f lavfi -i "testsrc2=size=320x180:rate=24:duration=2" -f lavfi -i "sine=frequency=660:sample_rate=44100:duration=2" -c:v libx264 -g 24 -bf 2 -pix_fmt yuv420p -c:a aac -shortest tests/fixtures/background-video.mp4
~~~

回归素材 background-long-audio.mkv 为 2 秒画面与 5 秒音轨，用于验证缺少视频流 duration 时不按音轨长度循环：

~~~powershell
ffmpeg -f lavfi -i "testsrc2=size=96x64:rate=24:duration=2" -f lavfi -i "sine=frequency=660:sample_rate=44100:duration=5" -c:v libx264 -g 24 -bf 2 -pix_fmt yuv420p -c:a aac tests/fixtures/background-long-audio.mkv
~~~

background-coarse-timebase.mp4 为 3 秒、2 fps、time_base=1/2 的全关键帧视频，验证 1.3 秒定位仍取得 1.0 秒帧：

~~~powershell
ffmpeg -f lavfi -i "testsrc2=size=96x64:rate=2:duration=3" -c:v libx264 -g 1 -bf 0 -pix_fmt yuv420p -video_track_timescale 2 tests/fixtures/background-coarse-timebase.mp4
~~~

background-delayed-video.mp4 的视频轨从源时间 1 秒开始、持续 2 秒；音轨从零开始、持续 3 秒，前 1 秒为 440 Hz、后 2 秒为 880 Hz。验证画面零点对应的原声为 880 Hz，且 Qt 定位与导出保持一致：

~~~powershell
ffmpeg -itsoffset 1 -i tests/fixtures/background-video.mp4 -f lavfi -i "sine=frequency=440:sample_rate=44100:duration=1" -f lavfi -i "sine=frequency=880:sample_rate=44100:duration=2" -filter_complex "[1:a][2:a]concat=n=2:v=0:a=1[a]" -map 0:v:0 -map "[a]" -c:v copy -c:a aac tests/fixtures/background-delayed-video.mp4
~~~

background-short-audio.mp4 为 4 秒视频与 1 秒 660 Hz 音轨，验证音轨结束后的静音区间和下一视频循环恢复播放：

~~~powershell
ffmpeg -f lavfi -i "testsrc2=size=96x64:rate=24:duration=4" -f lavfi -i "sine=frequency=660:sample_rate=44100:duration=1" -c:v libx264 -g 24 -bf 2 -pix_fmt yuv420p -c:a aac tests/fixtures/background-short-audio.mp4
~~~
