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

## 3. 性能分析方法

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

## 4. 优化过程

### 4.1 第一阶段：建立可验证的并发流水线

`VulkanOffscreenRenderer` 增加 `beginRender()` / `completeRender()` 两阶段接口。每个 frame slot 独立拥有：

- color target image 和 framebuffer；
- command buffer 和 fence；
- notes、静态 UI、动态 UI buffer；
- readback buffer。

`VideoExportService` 先填满可用 slot，再按 frame index 顺序完成最早帧。编码器仍是有序单消费者，因此不会改变帧顺序和 PTS。原有同步 `render()` 接口保留给预览和兼容调用。

这一步解决的是 GPU 与 CPU 的明显串行化，不能单独解决编码器吞吐不足的问题。

### 4.2 第二阶段：固定帧池和明确所有权

`FfmpegVideoEncoderWorker` 预分配 `capacity + 2` 个 `QImage`，通过 `acquireFrame()` / `submitFrame()` 配对使用：

```text
recycled frame pool -> producer fills -> bounded queue -> encoder thread
       ^                                      |
       +------------- encoded copy ----------+
```

编码线程确认 `QProcess::write()` 已复制输入数据后才回收帧，避免 QImage 在 FFmpeg 仍使用时被复用。队列、帧池和取消状态均受同一个互斥量保护，错误、取消和析构会唤醒 `notEmpty`、`notFull`、`frameAvailable` 等等待者。

这一步主要减少了逐帧分配、隐式 detach 和内存抖动，也让生产端等待点可观测。

### 4.3 第三阶段：降低 readback 和管道等待

Vulkan readback buffer 优先选择 `VK_MEMORY_PROPERTY_HOST_CACHED_BIT`，设备不支持时回退到普通 host-visible memory，并记录实际选择。

FFmpeg 输入管道调整为：

- Qt 待写缓冲上限约 4 MiB；
- 单次写入块约 1 MiB；
- 不在每帧结束时额外等待 Qt 缓冲清空；
- 通过 `finish()` 关闭 stdin，让 FFmpeg 按 EOF 正常排空和封装。

这避免了每帧 64 KiB 小写入和不必要的同步等待，同时保留 30 秒管道停滞保护、进程状态检查和取消响应。

### 4.4 第四阶段：硬件编码自适应

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

## 5. 实测指标

### 5.1 Readback 优化前后

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

### 5.2 1080p/4K 硬件编码基准

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

### 5.3 硬件利用率

对最终 benchmark 进行了 100 ms 级 NVIDIA 采样，共获得 30 条有效样本：

| 指标 | 平均 | 峰值 |
| --- | ---: | ---: |
| GPU 利用率 | 16.9% | 32% |
| Video Encode 利用率 | 27.1% | 100% |
| Video Decode 利用率 | 0% | 0% |

这说明硬件编码路径已经真正生效，但 Vulkan 绘制本身仍然是轻量场景，GPU 3D 利用率不会接近满载。低 3D 利用率不能作为导出效率低的单独证据。

最终集成诊断中，NVENC 选择结果为 `videoEncoder=h264_nvenc`、`hardwareAccelerated=true`。首次尺寸/参数组合的探测耗时约 220 ms，缓存命中后的探测记录约 18 ms；该成本只出现在每个新的参数组合首次启动时。

## 6. 正确性与回归验证

### 6.1 编码器测试

`video_encoder_roundtrip` 覆盖：

- 30/60 FPS；
- 50/100/120/150/200% 速度；
- 有音频和静音视频；
- 不完整 PCM、帧数不足、帧数超限；
- 固定帧池复用和帧序验证；
- 取消时阻塞在帧池上的生产者能被唤醒；
- FFprobe 帧数、CFR PTS、非负 DTS、音频格式和持续时间校验。

所有外部 FFmpeg 环境下的编码器测试通过。音频标记最大偏移为 1 个采样，局部相关度约 0.968，符合 AAC 有损编码的预期误差范围。

### 6.2 Vulkan 集成测试

`midi_play_video_export_tests --integration` 覆盖 30/60 FPS × 50/100/150/200% 共 8 组，检查：

- 所有帧均提交并编码；
- 至少两个 Vulkan frame slot 同时在途；
- MP4 非空且可解码；
- 场景画面非空且中途发生运动；
- 视频音频与独立 WAV 导出保持一致；
- 反复、节拍器和尾音时间线保持正确。

最终构建的 8 组真实 Vulkan 集成全部通过。

### 6.3 Distribution SOP

最终命令为：

```powershell
pwsh -NoLogo -NoProfile -File .\scripts\Build-Windows.ps1 `
  -EnvironmentFile .\build.env.psd1 `
  -BuildName video-export-performance-hwaccel-final-r2 `
  -PackageName midi-play-video-export-performance-hwaccel-final-r2 `
  -Jobs 8
```

结果：

- Release 编译通过；
- 16 个 CTest 中 14 个通过，2 个按环境跳过：`window_chrome_vulkan`、`video_encoder_roundtrip`；
- Windows 依赖闭包检查通过；
- CLI render smoke 通过；
- CLI MP3 smoke 通过；
- 发行包生成成功。

发行目录：

`dist/midi-play-video-export-performance-hwaccel-final-r2/`

包内 `build-info.json` 明确记录 `sourceHasLocalChanges=true`，表明该包对应当前未提交性能代码。

## 7. 关键经验

### 7.1 先测等待点，再决定并发度

三槽只代表最多三个 Vulkan command buffer 同时存在，不代表 GPU、readback、QImage、FFmpeg 和封装都在并发。必须分别记录 fence wait、GPU timestamp、QImage copy、queue wait 和 FFmpeg write，才能知道增加 slot 是否有效。

### 7.2 CPU/GPU 利用率低不等于线程没有并发

如果工作负载是轻量 shader + 大块内存拷贝 + 软件编码，GPU 可能只执行几十毫秒，CPU 也可能主要处于等待管道状态。此时应优化数据路径和编码后端，而不是盲目增加线程数量。

### 7.3 固定内存池的价值不仅是速度

固定帧池同时解决了内存峰值不可控、QImage 隐式共享、取消时对象生命周期不清晰和生产者/消费者所有权不明确等工程问题。性能优化需要把所有权写成接口约束，而不是依赖调用方习惯。

### 7.4 硬件编码探测必须使用真实运行验证

FFmpeg 的 `-encoders` 只说明编码器注册在二进制中，不能证明驱动、DLL、GPU 会话和当前尺寸都可用。实际短编码探测更可靠，但必须缓存结果并记录探测耗时，避免把探测成本误算为持续吞吐。

### 7.5 硬件编码不等于零拷贝

当前路径仍然是：

```text
Vulkan image -> host readback -> QImage/CPU memory -> FFmpeg stdin -> NVENC
```

NVENC 只替换了编码阶段，不能消除 Vulkan 到 CPU 的 readback。要继续提升 4K 吞吐，必须考虑 GPU 图像直接交给编码器的跨 API 资源共享。

### 7.6 正确性验证必须和性能验证同时存在

编码器更换、preset 修改和管道异步化都可能影响 PTS、DTS、AAC priming、颜色范围或帧序。每轮性能改动都应重复完整性校验、音频相关度检查和画面运动检查，不能只比较总耗时。

## 8. 当前限制与风险

1. 自动硬件路径目前只实现并验证 `h264_nvenc`。AMF/QSV 在本机不可用，尚未形成跨厂商统一策略。
2. NVENC 的 CQ 与 x264 CRF 不是数学等价关系；当前只保证质量档位单调和输出正确，尚未完成大规模主观质量/码率标定。
3. FFmpeg 不随发行包提供，运行时仍依赖用户配置的 PATH 或手动路径；具体 FFmpeg 构建的许可和编码器能力需要产品发行时单独审核。
4. 编码器仍是单进程、单有序消费路径；队列深度增大只能隐藏部分延迟，无法突破编码器自身吞吐。
5. Vulkan 和 NVENC 之间仍经过 CPU readback，显存到编码器的零拷贝尚未实现。
6. 当前硬件利用率结论来自 RTX 5060 Laptop GPU、对应驱动、Qt 6.8.3、MSVC 14.44 和当前 FFmpeg 构建，不能直接外推到所有用户设备。

## 9. 后续演进路线

### P0：完成跨设备质量和吞吐标定

- 在 NVIDIA、Intel、AMD 设备上分别测试硬件编码器初始化、质量、码率和播放器兼容性。
- 建立 1080p/4K、30/60 FPS、短曲/长曲的基准矩阵。
- 将编码器选择、首帧探测耗时、实际编码吞吐写入统一性能报告。

### P1：降低编码前的数据搬运

- 评估 Vulkan external memory 与 CUDA/NVENC 或 D3D11 video pipeline 的共享路径。
- 研究直接输出 NV12/P010，减少 CPU RGBA 到 YUV420 的重复转换。
- 评估 dedicated transfer queue、持久映射和批量 readback 对不同显卡的收益。

### P2：优化流水线调度

- 根据分辨率、编码器和历史队列等待时间自适应选择 frame slot 数量。
- 将 queue wait、fence wait 和 encoder write 建模为反馈信号，避免固定三槽在所有设备上使用同一配置。
- 在不破坏有序输出的前提下，评估多实例编码或独立 mux 线程的收益和复杂度。

### P3：发行策略收敛

- 明确发行包是否允许用户提供 GPL FFmpeg/libx264。
- 如需降低外部依赖，评估 Windows Media Foundation H.264/AAC 或经过许可审核的 LGPL FFmpeg 构建。
- 把硬件编码能力提示、软件回退原因和诊断导出纳入设置页或日志，而不改变默认用户流程。

## 10. 参考资料与本地证据

- [视频导出开发方案](video-export-development-plan.md)
- [最终集成诊断](../build/video-diagnostics-final-hwaccel.jsonl)
- [历史软件/并发基准](../build/video-diagnostics-benchmark.jsonl)
- [最终硬件利用率采样](../build/hardware-utilization-final-hwaccel.jsonl)
- [最终 Release 元数据](../dist/midi-play-video-export-performance-hwaccel-final-r2/build-info.json)
- [视频服务](../src/app/videoexportservice.cpp)
- [FFmpeg 编码器](../src/infrastructure/encoding/ffmpegvideoencoder.cpp)
- [FFmpeg 编码工作线程](../src/infrastructure/encoding/ffmpegvideoencoderworker.cpp)
- [Vulkan 离屏渲染器](../src/presentation/visualization/offscreen/vulkanoffscreenrenderer.cpp)
