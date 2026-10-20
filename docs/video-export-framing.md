# 导出画幅修复

> 文档范围：保留 2026-10-18 画幅修复时的设计及验收记录，测试数量、发布目录和当时的 Git 状态属于该次历史验证。当前构建与发布流程见 [README](../README.md#一键发行构建)，视频背景及新增门禁见 [视频背景（实验）](video-background.md)。

## 行为约定

视频页新增“保留小节标记栏”，默认勾选。每次打开导出页沿用配置默认值，不新增持久化设置。切换时重新生成预览，旧请求继续沿用取消标志和版本检查。

| 模式 | 场景逻辑尺寸 | 裁切区 | 布局约束 |
| --- | --- | --- | --- |
| 保留（默认） | 1280×720 | (0, 0, 1280, 720) | 原布局，无额外约束 |
| 移除 | (1280+G)×720 | (G, 0, 1280, 720) | 锁定扩展宽度 G 与虚拟白键数 |

G 由 1280×720 基准布局取得实际内容左边界并向上取整，不写死 50 或 1330。保留已有补白键规则得到的虚拟键数量，再将宽度和键数一并约束到扩展场景，避免重新布局导致补键数跳变。纯鼓曲 G 为原小节栏宽度，虚拟白键数为零。向上取整仅影响被裁掉的区域。

固定的是目标内容大小，不要求被裁掉的虚拟白键与实际琴键同宽。导出受约束时扩展区独立等分；实时窗口及默认保留模式继续使用原算法。混合曲的鼓区沿用原有比例和上下限，扩宽时可能触及 250 逻辑像素上限，这是重新布局的预期结果。

## 渲染路径

`calculateExportFramingPlan()` 在分配 GPU 资源之前验证 16:9 并计算计划。裁栏模式再次调用布局引擎，检查内容左边界、右边界及键盘底边是否满足约束。不满足时明确报错，不退回等比适配留边。

`scale = outputWidth / 1280`，`offsetX = -G * scale`，`offsetY = 0`，scissor 为完整输出矩形。预览输出 1024×576，比例 0.8；720p、1080p、4K 的比例为 1、1.5、3。Vulkan framebuffer、回读 QImage 与 FFmpeg 输入始终为最终输出尺寸。FFmpeg 现有 scale 滤镜只执行色彩范围/矩阵转换，没有设置缩放尺寸。

`VulkanScene` 缓存身份包含布局约束。同尺寸切换约束时清空音符横坐标缓存，防止琴键更新而音符仍使用旧位置。shader ABI、帧流水、音频时间线和编码质量参数保持现有协议。

诊断 JSONL 增加 `retainMeasureGutter`、`logicalSceneWidth/Height`、`cropLeft/Width/Height`、`geometryScale`，可直接核查布局与输出之间的映射。

## 验证

常规 `video_export_configuration` 覆盖 90 种投影音域/轨道组合及空图表、两种画幅模式、简谱开关、四档输出尺寸，共 1456 个规划组合；同时检查 UI 默认值及切换、非 16:9 拒绝、同尺寸布局约束切换后的音符缓存恢复。

可选验收（先配置 Qt/依赖 DLL 与 FFmpeg 的 PATH）：

```powershell
& './build/midi-play-v0_4_7-release/Release/midi_play_video_export_tests.exe' -platform offscreen --framing
& './build/midi-play-v0_4_7-release/Release/midi_play_video_export_tests.exe' -platform offscreen --integration
```

`--framing` 检查 48 组真实 Vulkan 帧，包括预览/720p/1080p/4K、旋律/混合/纯鼓、保留/移除、带背景及简谱/无背景及简谱，并验证最终像素尺寸和边缘覆盖。另将两种画幅模式分别编码为 720p/1080p/4K MP4，完整解码校验元数据及上下角落、两侧边缘像素。

设置 `MIDI_PLAY_FRAMING_CAPTURE_DIR` 可保存 720p 检查帧和六份短 MP4。设置 `MIDI_PLAY_FRAMING_REFERENCE_XML` 可额外读入本地 MusicXML，并以原曲再跑 16 组真实 Vulkan 画幅检查。测试不会写入或改动参考原文件。`--integration` 使用 640×360 完整音视频链路，覆盖 30/60 FPS、50/100/150/200% 速度以及反复/节拍器，两种画幅模式交叉执行。

## 本阶段边界

- 仅提供 16:9，非 16:9 明确失败；没有新增任意比例裁切策略。
- 背景继续在实际场景的下落区按现有对齐方式放置；移除左栏会同时裁去对应背景。用户主动选择“完整显示”背景时，图片自身可能留白，这与整帧居中留边不同。
- 该次画幅修复未改变背景解码尺寸策略。当前源码已支持 256–4096 px 最大边长，默认 2048 px，图片与视频共用；低分辨率素材在 4K 下的细节仍受源文件和解码尺寸限制。
- 未重写文字图集或 shader ABI，也未实现 DPR=1 的原生逻辑像素布局。

## 发布

以下保留该次验收使用的 SOP 命令与当时未提交代码的状态，不作为当前工作树或发行包状态说明：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/Build-Windows.ps1 -BuildName midi-play-v0_4_7-release -PackageName midi-play-ui-export-fix-windows-x64
```

脚本执行 Release 构建、常规 CTest、依赖部署、移除 SDK 后的依赖闭包与 CLI/渲染/MP3 冒烟。外部 FFmpeg 测试需在含 FFmpeg 的环境补跑。`build-info.json` 记录 `sourceHasLocalChanges=true`，用于表明发行包包含尚未提交的修复。

### 本次验收结果（2026-10-18）

- SOP 完成，发行目录为 `dist/midi-play-ui-export-fix-windows-x64/`。包内 GUI 与 Release 构建 GUI 的 SHA256 一致。
- 16 项常规 CTest 中，14 项在 SOP 中通过；外部 FFmpeg 编码往返检查在补充 PATH 后通过。桌面 `window_chrome_vulkan` 因当前桌面环境未产生首帧而跳过；该项不代表离屏渲染失败。
- 64 组真实离屏 GPU 帧通过（48 组夹具 + 16 组原曲），六份 720p/1080p/4K、保留/移除模式的 MP4 完整解码及边缘检查通过。ffprobe 均确认 SAR=1:1、DAR=16:9。
- 八组完整音视频集成导出通过，覆盖两种帧率、四种速度、反复和节拍器，以及两种画幅模式。
- 本机参考曲《琴弦呀琴弦》的裁栏计划为场景 1366×720、裁切 (86, 0, 1280, 720)；这个数值来自当前投影音域和补键规则，不是通用常量。
- 检查日志及示例位于 `build/framing-verification/`；没有改动用户原视频或乐谱，没有提交或推送。
