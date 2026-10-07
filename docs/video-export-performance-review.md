# 视频导出性能优化复盘

## 1. 文档目的

本文总结 `feat/video-export-concurrent-pipeline` 分支上的视频导出性能改进，记录问题定位方法、架构调整、实际指标、验证结果和后续演进建议。

本次工作遵循两个约束：

- 不改变视频导出的外部接口、时间线、音频同步、画面内容和安全提交语义。
- 优化代码先保留在本地工作区，使用完整 Windows Distribution SOP 编译和验证，不在本次复盘阶段提交性能改动。

当前基线提交为 `21bc46b perf: add video export diagnostics`，前一阶段并发流水线提交为 `7f23b22 feat: add concurrent video export pipeline`。

## 2. 原始问题与判断误区

用户观察到视频导出速度低于录屏，同时 GPU 利用率约 2%，CPU 利用率也不高。直觉上容易把问题归因于“三槽不够”或 Vulkan shader 太慢，但这两个判断都不成立。

原始流水线的实际结构是：

```text
生成一帧 -> 提交 Vulkan -> 等待 GPU fence -> readback -> QImage 拷贝
       -> 写入 FFmpeg stdin -> 等待管道/编码 -> 生成下一帧
```

即使增加三个 Vulkan frame slot，如果生产线程仍在最早帧上同步等待 readback，并且编码端只有一个阻塞的 FFmpeg stdin 消费者，GPU 也无法获得持续工作时间。低 GPU 利用率在这种场景下是结果，而不是根因。

视频导出还与录屏有本质区别：录屏通常复用已经完成的桌面合成和显示缓冲，导出则需要固定时间线、离屏绘制、GPU 到 CPU readback、颜色转换、编码和 MP4 封装。不能用录屏的吞吐模型直接推导离屏导出性能。

## 3. 原有架构详细描述

### 3.1 用户入口和任务快照

视频导出的入口在 `MainWindow::openExportDialog()`。入口只读取当前文档、SoundFont、FFmpeg 路径、Vulkan 导出状态、主题和背景，组装 `VideoExportOptions` 后交给导出对话框。视频按钮只有在 Vulkan 已准备完成且 FFmpeg 探测通过时才可用。

用户确认导出后，`MainWindow::startVideoExport()` 执行以下操作：

1. 保存不可变的 `shared_ptr<const MusicDocument>`，避免导出过程中读取正在编辑的文档。
2. 创建共享的取消标志和进度原子变量。
3. 在 GUI 线程创建 `QProgressDialog`、`QFutureWatcher` 和 80 ms 进度刷新定时器。
4. 通过 `QtConcurrent::run` 把 `VideoExportService::exportDocument()` 放到后台线程执行。
5. 后台任务结束后，GUI 线程只处理结果、恢复按钮状态并显示成功、取消或失败信息。

因此，GUI 线程不参与逐帧渲染、音频合成、FFmpeg 写入或 Vulkan 等待。取消的语义是设置原子标志，后台模块在音频块、Vulkan 等待、帧池等待、管道写入和完整性校验的边界检查它。

### 3.2 导出服务的阶段划分

`VideoExportService::exportDocument()` 是应用层编排器，不持有窗口对象，也不依赖实时播放时钟。其实际阶段如下：

```text
参数和资源校验
    -> PlaybackModel / VisualChart 快照
    -> ExportTimeline 固定帧数和音频采样数
    -> VulkanOffscreenRenderer 初始化
    -> OfflineAudioRenderer 生成 float PCM 临时文件
    -> FfmpegVideoEncoderWorker 启动 FFmpeg
    -> beginRender / completeRender / submitFrame 流水线
    -> close stdin / FFmpeg flush / MP4 封装
    -> ffprobe 元数据检查和完整解码检查
    -> QSaveFile 原子提交目标路径
```

失败发生在临时目录阶段时，不会覆盖用户已有的 MP4。只有编码结束、帧数/尺寸/帧率/音频/时长检查通过，且最终文件复制和 `QSaveFile::commit()` 成功后，任务才返回 `Success`。

### 3.3 时间线和音频架构

`ExportTimeline` 是视频和音频的共同时间基准：

- 输入音乐时间限制为 24 小时，使用绝对 `qint64` 微秒计算。
- 速度只缩放音乐时间，尾音使用输出墙钟时间追加。
- 视频帧数一次性按有理数向上量化，避免先转整数微秒造成边界多一帧。
- 30/60 FPS 下，44.1 kHz 分别对应 1470/735 个采样帧，视频每帧都对应整数数量的 PCM 样本。
- `musicPositionUs(frame)` 决定画面状态，`eventSample(musicUs)` 决定音频事件的采样位置。

音频阶段由 `OfflineAudioRenderer` 使用独立 FluidSynth 实例生成交错双声道 float PCM，按块写入临时文件。PCM 在启动 FFmpeg 前关闭，FFmpeg 只从文件读取音频，从 stdin 读取视频。这样避免同时管理两条跨进程写管道，也让音视频长度由明确的帧数和样本数决定。

### 3.4 Vulkan 离屏渲染架构

`VulkanOffscreenRenderer` 不创建窗口、surface 或 swapchain，而是独占一个 Vulkan instance、physical device、logical device、queue、command pool、render pass、pipeline、descriptor set、纹理和三组 frame slot。

每个 frame slot 的资源边界是：

| 资源 | 用途 | 生命周期 |
| --- | --- | --- |
| target image / framebuffer | 离屏颜色渲染目标 | renderer 初始化到销毁 |
| command buffer / fence | 提交和确认当前帧 | slot 生命周期 |
| notes buffer | 可见音符实例 | 按 scene revision 更新 |
| static UI buffer | 背景、网格等静态实例 | 按 revision 更新 |
| dynamic UI buffer | 当前时间相关 UI 实例 | 每帧更新 |
| readback buffer | `vkCmdCopyImageToBuffer` 结果 | slot 生命周期 |

`beginRender()` 只负责准备场景状态、更新当前 slot 的 buffer/纹理、录制命令并提交 queue；提交后返回轻量 `VulkanRenderTicket`。`completeRender()` 等待该 ticket 的 fence，必要时 invalidate 非 coherent memory，把 readback 内容拷贝到调用方提供的 `QImage`，然后释放 slot 的在途状态。

三个 slot 共用同一个 device 和 queue，但不跨线程共享 Vulkan 对象。纹理发生变化时，renderer 会等待旧 slot 完成后再更新共享纹理，防止在 GPU 仍采样时覆盖 atlas 或 background。

### 3.5 FFmpeg 编码架构

`FfmpegVideoEncoderWorker` 是一个有界单生产者/单消费者队列。它在自己的 `std::thread` 中创建并拥有 `FfmpegVideoEncoder` 和 `QProcess`，调用方线程只负责提交完整的 `QImage`。

内部有两类队列：

- `m_recycledFrames`：预分配并可重复使用的帧缓冲池，容量为 `capacity + 2`。
- `m_frames`：等待 FFmpeg 消费的有界提交队列，容量为 `capacity`。

帧状态转移为：

```text
recycled -> acquired -> filled -> submitted -> encoding -> recycled
                         |                       |
                         +---- failure/cancel --+--> released
```

`submitFrame()` 在队列满时等待 `m_notFull`，编码线程从 `m_frames` 取帧后立即通知生产者。`writeFrame()` 返回后，说明 Qt 已经复制了输入数据，编码线程才把 QImage 放回 `m_recycledFrames` 并通知 `m_frameAvailable`。这条约束避免了 QProcess 异步写入期间复用源内存。

### 3.6 同步、取消和错误边界

所有跨线程状态都由互斥量、条件变量和原子取消标志组合保护：

| 状态 | 生产者行为 | 编码线程行为 |
| --- | --- | --- |
| 正常 | 获取帧、完成 readback、提交队列 | 消费队列、写入 stdin、回收帧 |
| 队列满 | 等待 `notFull`，保留帧池边界 | 消费后唤醒生产者 |
| 帧池空 | 等待 `frameAvailable` | 写入完成后回收帧并唤醒 |
| 取消 | 立即停止提交并返回 Canceled | 清空未消费帧，跳过 finish，退出线程 |
| FFmpeg 失败 | 所有等待点被唤醒并返回错误 | 设置错误和 stop 状态，回收线程 |
| 析构 | 设置 abort/stop，唤醒所有条件变量 | join，保证 QProcess 和线程结束 |

这个边界很重要：仅设置取消标志并不能结束等待在条件变量、Vulkan fence 或 FFmpeg 管道上的线程；每一个等待点都必须有超时检查、错误状态或明确的唤醒路径。

### 3.7 封装、验证和安全提交

FFmpeg 的视频输入是 RGBA rawvideo，音频输入是 44.1 kHz、双声道、float PCM。编码输出先写到任务临时目录，完成后执行两层检查：

1. 如果存在 ffprobe，检查 stream 数量、尺寸、平均帧率、帧数、音频采样率、声道数、起始时间和持续时间。
2. 使用 FFmpeg 将视频和可选音频完整解码到 null 输出，检查解码过程、退出码和停滞超时。

验证成功后，服务以 1 MiB 块从临时 MP4 复制到 `QSaveFile`，最后原子提交到用户目标路径。用户看到的最终文件不会是尚未完成封装或尚未验证的中间文件。

## 4. 性能分析方法

本次没有只看单一总耗时，而是建立了从应用到硬件的分层观测：

| 层级 | 观测内容 | 目的 |
| --- | --- | --- |
| 时间线 | 提交帧数、完成帧数、编码帧数、帧率、输出尺寸 | 检查是否丢帧、重复帧或时间线漂移 |
| Vulkan CPU | 场景准备、buffer upload、命令录制、queue submit | 判断 CPU 侧命令准备是否占主导 |
| Vulkan GPU | timestamp、GPU frame time、fence wait | 区分 GPU 执行慢和 CPU 等待慢 |
| Readback | invalidate 时间、QImage 拷贝时间、host memory 属性 | 判断 GPU 到 CPU 的传输和缓存是否是瓶颈 |
| 编码队列 | 帧池等待、队列等待、峰值队列深度 | 判断生产端是否被编码端反压 |
| FFmpeg | stdin 写入时间、封装完成时间、实际编码器 | 判断软件编码、管道背压和 mux 是否占主导 |
| 硬件 | NVIDIA GPU、Video Encode、Decoder 利用率和显存 | 验证硬件编码是否真正运行 |

诊断默认关闭，只有设置 `MIDI_PLAY_VIDEO_DIAGNOSTICS=1` 时记录；路径由 `MIDI_PLAY_VIDEO_DIAGNOSTICS_PATH` 指定。因此诊断不会改变正常导出的 UI 或默认运行路径。

### 4.1 时间分解模型

对一段视频，应用可观察到的总耗时可以近似拆成：

```text
总耗时 = 音频合成
       + 视频生产流水线
       + FFmpeg flush / mux
       + 完整性校验
       + 最终文件提交
```

而视频生产流水线内部又包含：

```text
视频流水线 = 场景准备 + buffer 上传 + 命令录制 + queue submit
           + fence 等待 + readback invalidate + QImage 拷贝
           + 帧池等待 + 编码队列等待 + FFmpeg stdin 写入
```

这些项不是严格互斥的 wall-clock 区间：例如 GPU 计算可能与下一帧的 CPU 场景准备重叠，FFmpeg 编码也可能与 Vulkan 后续提交重叠。因此诊断字段用于回答“哪个阶段在阻塞谁”，不能简单把所有纳秒字段相加当成总耗时。

### 4.2 从基线到根因的推导

本次定位按以下顺序进行：

1. 先验证帧数和时间线，确认慢不是由重试、重复编码或隐藏的额外帧造成。
2. 对比 GPU timestamp 与 fence wait。如果 GPU frame time 很低而 fence wait 很高，说明 CPU 在等 GPU 或资源，而不是 shader 计算本身慢。
3. 把 QImage copy 从 readback invalidate 中分离。基线中 GPU 约 60 ms，但 QImage copy 超过 1.8 秒，说明主瓶颈在 CPU 读回和缓存属性。
4. 修复 readback 后重新观察 `encoderQueueWaitNs` 和 `ffmpegWriteNs`。两项成为主要耗时，说明瓶颈已经转移到编码端，而不是说明 Vulkan 优化失败。
5. 最后用 NVENC 与强制 `libx264` 的相同场景做 A/B，对照硬件 Video Encode 利用率，确认编码器路径确实改变了吞吐。

### 4.3 指标的正确解释

| 现象 | 可能含义 | 本次结论 |
| --- | --- | --- |
| GPU frame time 低、GPU utilization 低 | shader 计算量轻，或工作被 CPU/编码端间歇性供给 | 两者同时存在，主要瓶颈不在 shader |
| QImage copy 高 | readback memory 不适合 CPU 读取，或每帧发生分配/隐式拷贝 | host-cached + 固定 QImage 池有效 |
| `peakInFlightFrames=3` 但 queue wait 高 | Vulkan 已经有并发，编码消费速度更慢 | 继续增加 slot 的收益有限，需优化编码路径 |
| frame buffer wait 低、encoder queue wait 高 | 帧池本身不是瓶颈，提交队列被消费端反压 | 当前最终诊断中属于这种情况 |
| Video Encode 有峰值而 3D 利用率不高 | 硬件编码器工作，但图形渲染工作量轻 | 符合 Vulkan + NVENC 分工模型 |
| 1080p 总耗时收益小、4K 收益明显 | 固定验证/封装开销掩盖了编码吞吐收益 | 应按分辨率和曲长分别标定 |

### 4.4 为什么不能只看平均 CPU 利用率

导出任务由短暂 CPU burst、GPU queue、QProcess pipe backpressure 和外部编码线程交替组成。Windows 任务管理器的总 CPU 百分比会把等待管道、等待 fence 和其他空闲核心平均掉，无法区分“没有工作”和“被正确阻塞”。本次使用应用内计时器记录等待来源，再用 `nvidia-smi` 单独观察 GPU 和 Video Encode，才得到可操作的结论。

## 5. 优化过程

### 5.1 第一阶段：建立可验证的并发流水线

`VulkanOffscreenRenderer` 增加 `beginRender()` / `completeRender()` 两阶段接口。每个 frame slot 独立拥有：

- color target image 和 framebuffer；
- command buffer 和 fence；
- notes、静态 UI、动态 UI buffer；
- readback buffer。

`VideoExportService` 先填满可用 slot，再按 frame index 顺序完成最早帧。编码器仍是有序单消费者，因此不会改变帧顺序和 PTS。原有同步 `render()` 接口保留给预览和兼容调用。

这一步解决的是 GPU 与 CPU 的明显串行化，不能单独解决编码器吞吐不足的问题。

实施时没有把 Vulkan 对象拆到多个线程。原因是当前渲染器使用单 queue、共享 descriptor set 和共享 atlas/background，跨线程提交会引入 queue ownership、资源生命周期和调试层同步问题。采用单渲染线程顺序录制/提交、多个 slot 保持 GPU 在途，已经能消除主要的“提交一帧就等待一帧”串行化，同时保留 Vulkan 资源模型的可验证性。

`beginRender()` 返回 false 时并不立即视为失败：如果没有错误且仍有 pending ticket，服务先完成最早帧再重试当前 frame index。这使 slot 暂时耗尽成为正常背压，而不是异常；只有 renderer 返回错误文本、取消或所有 pending 都为空时才结束任务。

### 5.2 第二阶段：固定帧池和明确所有权

`FfmpegVideoEncoderWorker` 预分配 `capacity + 2` 个 `QImage`，通过 `acquireFrame()` / `submitFrame()` 配对使用：

```text
recycled frame pool -> producer fills -> bounded queue -> encoder thread
       ^                                      |
       +------------- encoded copy ----------+
```

编码线程确认 `QProcess::write()` 已复制输入数据后才回收帧，避免 QImage 在 FFmpeg 仍使用时被复用。队列、帧池和取消状态均受同一个互斥量保护，错误、取消和析构会唤醒 `notEmpty`、`notFull`、`frameAvailable` 等等待者。

这一步主要减少了逐帧分配、隐式 detach 和内存抖动，也让生产端等待点可观测。

帧池容量不是越大越好。`capacity + 2` 的设计分别覆盖有界提交队列、正在被编码线程消费的帧和生产者当前填写的帧，内存规模可由输出尺寸直接估算。以 RGBA 4K 为例，单帧约 33 MiB，容量从 3 随意扩大到 16 会额外占用数百 MiB，却不能提高单个 FFmpeg 编码器的吞吐。固定容量同时给取消和错误路径提供了清晰的上限。

### 5.3 第三阶段：降低 readback 和管道等待

Vulkan readback buffer 优先选择 `VK_MEMORY_PROPERTY_HOST_CACHED_BIT`，设备不支持时回退到普通 host-visible memory，并记录实际选择。

FFmpeg 输入管道调整为：

- Qt 待写缓冲上限约 4 MiB；
- 单次写入块约 1 MiB；
- 不在每帧结束时额外等待 Qt 缓冲清空；
- 通过 `finish()` 关闭 stdin，让 FFmpeg 按 EOF 正常排空和封装。

这避免了每帧 64 KiB 小写入和不必要的同步等待，同时保留 30 秒管道停滞保护、进程状态检查和取消响应。

这里有一个重要的 EOF 语义：`QProcess::write()` 只保证数据进入 Qt 的待写缓冲，不代表 FFmpeg 已完成编码；过早关闭或在每帧结束时等待所有待写数据，都会把应用重新变成同步生产者。现在由最后一次 `submitFrame()` 之后统一调用 `closeWriteChannel()`，让 FFmpeg 自己排空 stdin、完成编码器 flush 和 MP4 mux。这样既能保持帧顺序，也避免生产线程在每帧边界重复支付 pipe drain 成本。

### 5.4 第四阶段：硬件编码自适应

实际探测发现本机 FFmpeg 支持并能初始化 `h264_nvenc`，AMF 和 QSV 不可用，`libx264` 可用。因此编码器选择策略为：

1. 对实际输出尺寸、帧率和质量参数执行短时真实编码探测。
2. 探测成功则使用 `h264_nvenc`。
3. 探测失败则回退 `libx264`。
4. 使用内部环境变量 `MIDI_PLAY_VIDEO_ENCODER=software` 可强制软件基线。
5. 按 FFmpeg 路径、尺寸、FPS、CRF 和选择偏好缓存探测结果，避免每次导出重复探测。

硬件和软件路径使用不同的码控参数：

| 路径 | 质量参数 | preset | 说明 |
| --- | --- | --- | --- |
| `libx264` | CRF | `medium` | 保留原有软件质量基线 |
| `h264_nvenc` | VBR + CQ | `p4` | 将现有质量档位映射到 NVENC CQ |

两条路径都保留 `-bf 0`、固定 GOP、BT.709 颜色转换、CFR 和现有 MP4 edit list 策略，避免硬件加速引入时间线或播放器兼容性回归。

硬件路径的加入经过了三个防护：

1. 探测命令使用真实尺寸、真实 FPS、相同的颜色转换和实际质量参数，而不是只读取 `-encoders` 列表。
2. 选择结果按 FFmpeg 可执行文件绝对路径、尺寸、FPS、CRF 和偏好缓存，避免每个短导出都启动一次探测进程。
3. 诊断记录 `videoEncoder`、`hardwareAccelerated` 和 `encoderSelectionNs`，因此性能报告可以区分“首个任务探测成本”和“稳定编码吞吐”。

NVENC 没有可用驱动、尺寸不满足硬件限制或运行时 DLL 不完整时，会回到 `libx264`。回退不是静默改变输出语义：帧率、颜色、PTS、AAC、完整性校验和安全提交仍走同一条业务路径。

## 6. 实测指标

### 6.1 Readback 优化前后

在 1,507 帧真实 Vulkan 集成导出中，优化前的主要数据为：

- 视频流水线约 2.2～2.5 秒；
- QImage 拷贝约 1,889～2,127 ms；
- GPU 执行约 60 ms。

启用 host-cached readback、固定帧池和并发流水线后：

- 视频流水线约 1,039 ms；
- QImage 拷贝约 38.46 ms；
- GPU 执行约 56.72 ms；
- FFmpeg 写入约 1,023.31 ms；
- 编码队列等待约 854.59 ms；
- `readbackHostCached=true`，峰值 Vulkan 在途帧数为 3，峰值编码队列深度为 3。

结论是：QImage 拷贝从主要瓶颈下降为次要开销，剩余时间主要由编码和管道背压决定。

### 6.2 1080p/4K 硬件编码基准

最终 Release 构建在 NVIDIA GeForce RTX 5060 Laptop GPU 上运行 `--benchmark`，使用 NVENC 自动路径：

| 输出 | 帧数 | 视频流水线 | 总耗时 | 纯 Vulkan 渲染 |
| --- | ---: | ---: | ---: | ---: |
| 1920×1080 | 140 | 约 552～574 ms | 约 1,325～1,350 ms | 约 77 ms / 120 帧 |
| 3840×2160 | 140 | 约 1,912～1,948 ms | 约 2,964～3,049 ms | 约 306～310 ms / 120 帧 |

同一最终构建强制使用软件 `libx264` 时：

| 输出 | 视频流水线 | 总耗时 |
| --- | ---: | ---: |
| 1920×1080 | 约 552 ms | 约 1,150 ms |
| 3840×2160 | 约 2,592 ms | 约 3,896 ms |

1080p 测试中，场景短、验证和封装固定成本占比高，因此总耗时不一定优于软件路径；4K 测试更能反映编码吞吐，NVENC 流水线约比软件路径快 26%。

### 6.3 硬件利用率

对最终 benchmark 进行了 100 ms 级 NVIDIA 采样，共获得 30 条有效样本：

| 指标 | 平均 | 峰值 |
| --- | ---: | ---: |
| GPU 利用率 | 16.9% | 32% |
| Video Encode 利用率 | 27.1% | 100% |
| Video Decode 利用率 | 0% | 0% |

这说明硬件编码路径已经真正生效，但 Vulkan 绘制本身仍然是轻量场景，GPU 3D 利用率不会接近满载。低 3D 利用率不能作为导出效率低的单独证据。

最终集成诊断中，NVENC 选择结果为 `videoEncoder=h264_nvenc`、`hardwareAccelerated=true`。首次尺寸/参数组合的探测耗时约 220 ms，缓存命中后的探测记录约 18 ms；该成本只出现在每个新的参数组合首次启动时。

## 7. 正确性与回归验证

### 7.1 编码器测试

`video_encoder_roundtrip` 覆盖：

- 30/60 FPS；
- 50/100/120/150/200% 速度；
- 有音频和静音视频；
- 不完整 PCM、帧数不足、帧数超限；
- 固定帧池复用和帧序验证；
- 取消时阻塞在帧池上的生产者能被唤醒；
- FFprobe 帧数、CFR PTS、非负 DTS、音频格式和持续时间校验。

所有外部 FFmpeg 环境下的编码器测试通过。音频标记最大偏移为 1 个采样，局部相关度约 0.968，符合 AAC 有损编码的预期误差范围。

### 7.2 Vulkan 集成测试

`midi_play_video_export_tests --integration` 覆盖 30/60 FPS × 50/100/150/200% 共 8 组，检查：

- 所有帧均提交并编码；
- 至少两个 Vulkan frame slot 同时在途；
- MP4 非空且可解码；
- 场景画面非空且中途发生运动；
- 视频音频与独立 WAV 导出保持一致；
- 反复、节拍器和尾音时间线保持正确。

最终构建的 8 组真实 Vulkan 集成全部通过。

## 8. 关键经验

### 8.1 先测等待点，再决定并发度

三槽只代表最多三个 Vulkan command buffer 同时存在，不代表 GPU、readback、QImage、FFmpeg 和封装都在并发。必须分别记录 fence wait、GPU timestamp、QImage copy、queue wait 和 FFmpeg write，才能知道增加 slot 是否有效。

可复用的判断规则是：先找最长的“阻塞别人”的等待，再决定并发边界。如果 `fenceWaitNs` 很高，应检查 GPU queue、资源冲突和 readback；如果 `qimageCopyNs` 很高，应检查内存类型、stride、格式转换和分配；如果 `encoderQueueWaitNs` 很高而 `encoderFrameBufferWaitNs` 很低，应优先优化编码器或减少输入搬运。把所有等待都归类为“GPU 慢”会导致错误的优化方向。

### 8.2 CPU/GPU 利用率低不等于线程没有并发

如果工作负载是轻量 shader + 大块内存拷贝 + 软件编码，GPU 可能只执行几十毫秒，CPU 也可能主要处于等待管道状态。此时应优化数据路径和编码后端，而不是盲目增加线程数量。

利用率是资源占用比例，不是吞吐瓶颈的定位结果。一个任务可以在 GPU 只有 10% 利用率时达到当前场景的最大吞吐，也可以在 GPU 90% 利用率时因为 readback 或封装仍然很慢。性能报告应同时给出 wall-clock、每阶段耗时、帧数和硬件计数器，避免用单一百分比替代因果分析。

### 8.3 固定内存池的价值不仅是速度

固定帧池同时解决了内存峰值不可控、QImage 隐式共享、取消时对象生命周期不清晰和生产者/消费者所有权不明确等工程问题。性能优化需要把所有权写成接口约束，而不是依赖调用方习惯。

在 C++/Qt 代码中，`QImage` 的值语义很容易掩盖真实拷贝：移动一个 QImage 对象通常很便宜，但对共享数据执行 `bits()`、修改像素或跨线程持有时可能触发 detach。通过“acquire 后由生产者填充，submit 后只能由 worker 回收”的协议，可以把 detach 风险变成可检查的格式约束。测试还应验证 buffer 地址集合有界、像素内容和帧序未改变，而不只是验证输出文件存在。

### 8.4 硬件编码探测必须使用真实运行验证

FFmpeg 的 `-encoders` 只说明编码器注册在二进制中，不能证明驱动、DLL、GPU 会话和当前尺寸都可用。实际短编码探测更可靠，但必须缓存结果并记录探测耗时，避免把探测成本误算为持续吞吐。

探测本身也有工程成本：它会启动额外的 FFmpeg 进程，可能触发驱动初始化，并在笔记本 GPU 上产生电源状态切换。正确做法是把探测计时单独记录，缓存键包含所有影响能力的参数，并提供软件强制模式做可重复 A/B。没有这些信息，第一次导出变慢时很容易误判为硬件编码收益不足。

### 8.5 硬件编码不等于零拷贝

当前路径仍然是：

```text
Vulkan image -> host readback -> QImage/CPU memory -> FFmpeg stdin -> NVENC
```

NVENC 只替换了编码阶段，不能消除 Vulkan 到 CPU 的 readback。要继续提升 4K 吞吐，必须考虑 GPU 图像直接交给编码器的跨 API 资源共享。

零拷贝方案也不是无条件更优。它需要处理 Vulkan image layout、external memory handle、CUDA/D3D11 interop、NV12 转换、同步 semaphore 和不同驱动的兼容性；一旦失败，错误排查难度会高于当前可读回的 CPU 路径。因此应先用当前诊断证明 readback 已成为剩余主要成本，再以独立实验分支验证跨 API 共享，不应在没有基线的情况下直接替换稳定路径。

### 8.6 正确性验证必须和性能验证同时存在

编码器更换、preset 修改和管道异步化都可能影响 PTS、DTS、AAC priming、颜色范围或帧序。每轮性能改动都应重复完整性校验、音频相关度检查和画面运动检查，不能只比较总耗时。

性能测试至少要保留三类断言：

- 结构断言：尺寸、FPS、帧数、音频采样率、声道、起始时间和时长。
- 内容断言：音频标记位置、静音区、画面非空和中途运动。
- 生命周期断言：取消可收敛、失败可唤醒、线程可 join、临时输出不会覆盖旧文件。

只有三类断言同时通过，才能把“更快”解释为可交付的优化，而不是牺牲了时间线或稳定性的 benchmark 偶然值。

### 8.7 诊断必须低侵入且可关闭

诊断代码应该通过环境变量或显式配置启用，默认路径不写 JSON、不创建额外采样线程、不改变队列容量和等待策略。字段应记录设备、参数、阶段计时和计数器，但不要把诊断输出混入用户可见的错误文本。

本次将 renderer 和 encoder 的累计计时保存在各自实例中，只在导出成功后汇总到 JSON；这保证失败任务仍能通过错误返回，正常用户也不会因为日志写入阻塞导出。后续新增指标时应保持 schema 版本和字段语义稳定，避免不同构建的结果无法比较。

### 8.8 性能优化必须绑定可复现的构建和硬件上下文

编码器、驱动、Qt、Vulkan loader 和电源策略都会影响结果。报告不能只写“4K 快了多少”，还应同时记录源码 revision、是否有本地修改、编译器、Qt、GPU、驱动、FFmpeg 版本、输出尺寸、FPS、CRF、音频开关和采样时间。

Distribution SOP 的价值不仅是产出安装目录，还能验证依赖闭包、Release 配置、CLI smoke 和资源部署没有被性能改动破坏。性能结论必须引用一个明确的构建目录和诊断文件，不能引用开发机上无法复现的临时运行结果。

## 9. 当前限制与风险

1. 自动硬件路径目前只实现并验证 `h264_nvenc`。AMF/QSV 在本机不可用，尚未形成跨厂商统一策略。
2. NVENC 的 CQ 与 x264 CRF 不是数学等价关系；当前只保证质量档位单调和输出正确，尚未完成大规模主观质量/码率标定。
3. FFmpeg 不随发行包提供，运行时仍依赖用户配置的 PATH 或手动路径；具体 FFmpeg 构建的许可和编码器能力需要产品发行时单独审核。
4. 编码器仍是单进程、单有序消费路径；队列深度增大只能隐藏部分延迟，无法突破编码器自身吞吐。
5. Vulkan 和 NVENC 之间仍经过 CPU readback，显存到编码器的零拷贝尚未实现。
6. 当前硬件利用率结论来自 RTX 5060 Laptop GPU、对应驱动、Qt 6.8.3、MSVC 14.44 和当前 FFmpeg 构建，不能直接外推到所有用户设备。

## 10. 后续演进路线

### 10.1 P0：完成跨设备质量和吞吐标定

- 在 NVIDIA、Intel、AMD 设备上分别测试硬件编码器初始化、质量、码率和播放器兼容性。
- 建立 1080p/4K、30/60 FPS、短曲/长曲的基准矩阵。
- 将编码器选择、首帧探测耗时、实际编码吞吐写入统一性能报告。

### 10.2 P1：降低编码前的数据搬运

- 评估 Vulkan external memory 与 CUDA/NVENC 或 D3D11 video pipeline 的共享路径。
- 研究直接输出 NV12/P010，减少 CPU RGBA 到 YUV420 的重复转换。
- 评估 dedicated transfer queue、持久映射和批量 readback 对不同显卡的收益。

### 10.3 P2：优化流水线调度

- 根据分辨率、编码器和历史队列等待时间自适应选择 frame slot 数量。
- 将 queue wait、fence wait 和 encoder write 建模为反馈信号，避免固定三槽在所有设备上使用同一配置。
- 在不破坏有序输出的前提下，评估多实例编码或独立 mux 线程的收益和复杂度。

### 10.4 P3：发行策略收敛

- 明确发行包是否允许用户提供 GPL FFmpeg/libx264。
- 如需降低外部依赖，评估 Windows Media Foundation H.264/AAC 或经过许可审核的 LGPL FFmpeg 构建。
- 把硬件编码能力提示、软件回退原因和诊断导出纳入设置页或日志，而不改变默认用户流程。

## 11. 参考资料与本地证据

- [视频导出开发方案](video-export-development-plan.md)
- [视频服务](../src/app/videoexportservice.cpp)
- [FFmpeg 编码器](../src/infrastructure/encoding/ffmpegvideoencoder.cpp)
- [FFmpeg 编码工作线程](../src/infrastructure/encoding/ffmpegvideoencoderworker.cpp)
- [Vulkan 离屏渲染器](../src/presentation/visualization/offscreen/vulkanoffscreenrenderer.cpp)
