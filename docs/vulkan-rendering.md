# Vulkan 资源生命周期与琴键闪烁回归

## 已确认的触发条件

在 Qt 6.8.3、RTX 5060 Laptop GPU 上，`concurrentFrameCount()` 为 2，交换链图像数为 3。
旧实现用 `currentFrame()` 索引可写的音符、UI、文字图集和上传缓冲。
Qt 在 `startNextFrame()` 前等待的是当前交换链图像的绘制 fence；轮转帧的 fence 用于图像获取，不能据此认定同编号的应用缓冲已经不再被 GPU 使用。

连续播放用户提供的 MIDI，在绘制命令末尾设置 GPU completion event、复用前查询其状态，旧实现反复出现 `DRAW STILL IN FLIGHT`。
UI 实例内容与本帧绘制 offset 可能来自不同帧，从而影响整排琴键，包括没有被按下的白键。
逐帧调用 `QVulkanWindow::grab()` 会等待 GPU，掩盖这一问题，因此单张截图不能作为生命周期正确的证据。

完整 synchronization validation 还识别出两类问题：

- Qt 默认 render pass 的初始布局转换没有显式衔接图像获取信号量的 `COLOR_ATTACHMENT_OUTPUT` 等待阶段。
- Qt 内部 acquire/present 信号量也按轮转帧复用，连续播放会触发 `VUID-vkAcquireNextImageKHR-semaphore-01779` 和 `VUID-vkQueueSubmit-pSignalSemaphores-00067`。应用缓冲改为按图像分配后，这两项框架内部问题仍会出现。

## 当前约束

- 每张交换链图像独立持有 framebuffer、静态 UI、动态 UI、音符、文字图集、staging 和 descriptor。它们随交换链创建、等待 GPU 后释放，不能与 `currentFrame()` 混用。
- 使用仅含颜色附件的 render pass，并显式声明获取图像到颜色附件读写的外部依赖。该 2D 场景无需深度附件。
- `completePresentation()` 在 Qt 提交和呈现后完成相应队列，避免框架下一次获取图像时过早复用信号量。图形和呈现队列不同的设备使用 device wait，因为 Qt 公共接口不暴露呈现队列。
- 上述 Qt 兼容边界会限制 CPU/GPU 多帧并行；它是有意的正确性取舍。升级 Qt 后，只有连续呈现通过同等生命周期和 validation 检查，才能移除这一步。不能只删除等待来提高测试帧率。

## UI 批次

静态批次包含音高背景、键盘底色、白键、黑键和鼓键底色，在图表、逻辑布局或主题变化时重建和上传。它不引用文字图集，DPI 由 push constants 处理。

动态批次包含网格、击键光效、高亮、文字和覆盖层。每张图像保留自己的 CPU 副本；实例数量相同时，仅上传发生变化的连续区间。数量变化时上传完整动态批次。音符仍使用独立 revision 和缓冲。

层顺序为背景、音符、击键效果、白键、黑键、文字、覆盖层。每层先静态后动态，确保动态白键不会盖住静态黑键。文字留在动态批次，图集回收或字体变化不会使静态琴键的 UV 失效。

主题由共享 `AppTheme` 提供。切换主题会推进静态 UI、音符实例和 CPU 材质版本，动态批次使用同一主题重新生成；清屏颜色也来自当前场景。每张交换链图像在原有 fence 保护下更新自己的缓冲，主题切换不重建窗口、设备或交换链，不清空主题无关的文字 alpha 图集。

## 验证

普通 CTest 包含静态琴键缓存回归。GPU 检查需要桌面会话和可用 Vulkan 驱动，不能使用 `-platform offscreen`。在配置了 Qt DLL 路径的开发终端中执行：

```powershell
$env:VK_LAYER_VALIDATE_SYNC = '1'
.\build\visual-polish-release\Release\midi_play_note_visual_tests.exe --vulkan-stress
.\build\visual-polish-release\Release\midi_play_note_visual_tests.exe --vulkan-stress --midi 'C:\path\reproducer.mid'
.\build\visual-polish-release\Release\midi_play_note_visual_tests.exe --snapshots build/vulkan-diagnostics --vulkan
.\build\visual-polish-release\Release\midi_play_presentation_tests.exe --vulkan-smoke
```

`--vulkan-stress` 默认生成 4,096 个音符，先在 2560×1440 逻辑尺寸下连续呈现至少 900 帧，期间交替切换主题和简谱显隐；再缩放并按时间采样每个琴键的内部像素，与共享音符状态计算出的颜色比较，采样也交替使用深浅主题。提供 `--midi` 时使用指定乐曲的完整时间范围。测试启用 GPU completion event 审计，有验证层时启用该层，任何收到的 validation error 都会使测试失败。`--snapshots ... --vulkan` 输出两套主题和简谱显隐的组合截图，并比较 Qt/Vulkan 的音符与琴键颜色。

`MIDI_PLAY_VULKAN_VALIDATE_RESOURCES=1` 可单独启用资源复用审计；`QT_LOGGING_RULES=midi_play.vulkan.debug=true` 可查看图像/帧编号、缓存 revision、实例数量和缓冲容量。发行版默认不启用逐帧诊断。
