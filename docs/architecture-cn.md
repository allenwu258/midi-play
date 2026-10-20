# MIDI Play 项目架构说明

本文是 MIDI Play 的详细架构文档，面向需要阅读、调试、扩展或部署项目的开发者。文档以当前源码为准，描述模块边界、运行时对象、数据流、线程模型和关键操作流程。2026-10-20 已同步视频背景（实验）实现；CMake 项目版本仍为 0.4.7，该版本号不代表已发布的 v0.4.7 包包含本分支新增功能。

本文只记录已经存在于代码中的职责和约束。它不定义新的产品需求，也不替代各模块的接口注释、测试用例和专项设计文档。与具体实现相关的说明会给出源码路径，便于从架构概念继续追踪实现。

## 1. 项目定位

MIDI Play 是一个面向 Windows x64 的 MIDI 播放和下落音符可视化应用。它把 MIDI、KAR 和 MusicXML 文件转换为统一的音乐文档，使用 FluidSynth 合成音频，并使用 Qt Widgets 或 Vulkan 绘制播放场景。项目同时提供命令行程序，用于解析验证、音频导出和离屏截图。

项目的核心目标是：

- 使用统一的音乐时间模型承载不同输入格式；
- 在播放、可视化和导出之间复用同一份音乐语义；
- 保证播放线程的事件调度不依赖界面刷新频率；
- 在图形设备可用时使用 Vulkan，在不具备 Vulkan 能力时使用传统 Qt 绘制；
- 通过清晰的应用服务隔离界面、领域逻辑和外部库。

当前范围不包括完整的五线谱编辑器、MuseScore 工程编辑、网络资源加载、外部硬件 MIDI 输出和跨平台发行基线。MusicXML 解析面向播放和可视化所需语义，不等价于完整的排版或编辑 DOM。

## 2. 技术栈与构建目标

项目使用以下主要技术：

| 类别 | 技术 | 用途 |
| --- | --- | --- |
| 语言 | C++20 | 领域模型、应用服务、基础设施和 Qt 界面 |
| UI | Qt 6.8 Widgets/Gui/Core/Concurrent/Xml | 窗口、控件、异步任务、XML 读取和图像 |
| 音频 | FluidSynth 动态库 | SoundFont 加载、实时合成、离线合成 |
| 视频原声 | Qt 6 Multimedia | 独立播放第一条原声音轨，跟随音乐传输状态 |
| 图形 | Qt QPainter、可选 Vulkan | 传统绘制、实时 Vulkan 窗口和离屏渲染 |
| 媒体与编码 | FFmpeg 库及命令行工具、LAME、WebP | 进程内视频帧解码、原声处理、视频封装、MP3/WAV 输出和背景图片支持 |
| 构建 | CMake、vcpkg、MSVC/Ninja | 依赖发现、编译、测试和 Windows 发行 |

顶层 CMakeLists.txt 先配置 Qt（含 Multimedia）、FFmpeg 库、Threads、WebP 和 LAME，再根据 Qt Vulkan 能力、Vulkan SDK 和 glslangValidator 是否存在决定是否定义 MIDI_PLAY_HAS_VULKAN。FluidSynth 不作为链接时的必需符号使用，运行时由 QLibrary 动态加载；视频解码器则链接 FFmpeg 库，传统 Qt 构建也需要该依赖。

构建目标分为两类：

| 目标 | 入口 | 作用 |
| --- | --- | --- |
| midi_play_core | 所有 app、domain、infrastructure、presentation 公共源文件 | 静态库，承载 GUI 和 CLI 共享的业务能力 |
| midi_play | src/main.cpp | Qt GUI 播放器 |
| midi_play_cli | src/cli/main.cpp | Console 解析、音频导出、离屏渲染和诊断程序 |

测试目标在启用测试配置时由 CMake 生成，覆盖领域可视化、播放会话、节拍器、呈现后端、窗口边框、SoundFont、音频导出、视频背景和视频编码等路径。

## 3. 总体架构

### 3.1 分层关系

~~~mermaid
flowchart TB
    UI[Presentation Qt Widgets / QPainter / Vulkan]
    APP[Application 应用编排]
    MUSIC[Domain Music 音乐事实与时间语义]
    PLAY[Domain Playback 播放事件与传输]
    VIS[Domain Visualization 可视化投影与索引]
    SET[Domain Settings 设置值对象]
    READ[Infrastructure Readers MIDI / MusicXML]
    AUDIO[Infrastructure Audio FluidSynth]
    MEDIA[Infrastructure Media 视频帧解码]
    ENCODE[Infrastructure Encoding FFmpeg / LAME / WebP]
    STORE[Infrastructure Settings QSettings]
    EXT[(文件系统、动态库、GPU、音频设备)]

    UI --> APP
    UI --> VIS
    UI --> MEDIA
    APP --> MUSIC
    APP --> PLAY
    APP --> VIS
    APP --> SET
    APP --> READ
    APP --> AUDIO
    APP --> ENCODE
    APP --> MEDIA
    APP --> STORE
    READ --> MUSIC
    PLAY --> MUSIC
    VIS --> MUSIC
    AUDIO --> PLAY
    ENCODE --> VIS
    ENCODE --> AUDIO
    STORE --> SET
    AUDIO --> EXT
    MEDIA --> EXT
    ENCODE --> EXT
    UI --> EXT
~~~

依赖方向的关键约束如下：

1. Presentation 负责用户交互和呈现，不直接解析 MIDI/XML，也不直接操作 FluidSynth。
2. Application 负责把输入、领域模型、播放会话、设置和导出任务连接起来。
3. Domain Music 不依赖文件格式和具体音频后端，保存可以被多个消费者复用的音乐事实。
4. Domain Playback 把音乐事实转换为有序播放事件，并管理传输状态和恢复状态。
5. Domain Visualization 把音乐文档投影为不可变的视觉数据，供 Qt、Vulkan 和 CLI 复用。
6. Infrastructure 负责外部格式、动态库、进程、文件和平台服务的适配。

### 3.2 运行时主链路

完整的主链路可以概括为：

~~~text
输入文件
  -> MusicReaderRegistry
  -> 格式 Adapter
  -> Parser / Normalizer / Builder
  -> MusicDocument
  -> PlaybackTimeline / PlaybackModel
  -> PlaybackSession / PlaybackController
  -> ThreadedPlaybackAudioService
  -> FluidSynthEngine

MusicDocument
  -> PlaybackVisualizationProjector
  -> VisualChart
  -> VisibleNoteIndex / VisibleNoteWindowCache
  -> PlaybackSceneState / SceneLayoutEngine
  -> FallingNotesView 或 FallingNotesVulkanWindow
~~~

播放和视觉共享音乐时间语义，但不共享调度职责。PlaybackSession 是音符事件的权威调度者；界面只接收节流后的 transport 位置样本。这样，UI 卡顿或刷新率变化不会改变 MIDI 事件的计划时间。

## 4. 目录与模块职责

### 4.1 app：应用服务和跨模块编排

目录：src/app

| 对象 | 职责 |
| --- | --- |
| PlayerApplicationService | 接收打开文件、播放、暂停、seek、音源选择和图形模式操作；异步构建项目并向 UI 发布状态 |
| SettingsService | 读取、规范化、修改和持久化 PlayerSettings；通过 Qt signal 通知各消费者 |
| AudioExportService | 把音乐文档离线合成为 WAV 或 MP3 |
| OfflineAudioRenderer | 使用 FluidSynth 离线生成交错或分离的浮点 PCM 数据 |
| VideoExportService | 编排 Vulkan 离屏渲染、离线音频和 FFmpeg 视频封装 |
| ExportTimeline | 把音乐时长、视频帧率和音频采样率映射到统一的导出帧时间 |
| FfmpegService | 探测和缓存 FFmpeg 可执行文件及编码能力 |
| ISettingsStore | 设置持久化的抽象接口 |

Application 层不保存输入格式的解析细节，也不把具体 Qt 控件暴露给领域对象。它可以替换 reader、音频后端或设置存储实现，而无需改变 MainWindow 的基本交互契约。

### 4.2 domain/music：音乐文档和时间语义

目录：src/domain/music

核心对象：

- MusicDocument：统一音乐文档。包含标题、轨道、音符、小节、tempo、拍号、调号、歌词、标记、控制器、踏板和重复标记。
- Track：保存一个演奏轨道的通道、乐器、打击乐属性，以及该轨道的音符和控制事件。
- NoteEvent：保存音高、力度、起止 tick、声部、谱表、连音、演奏法、和弦、踏板释放时间等信息。
- PlaybackSegment：把源小节区间映射到反复展开后的输出时间线。
- PlaybackTimeline：在 MusicDocument 定稿后预计算可共享的播放段索引和微秒时长。
- MusicRhythmGrid：根据拍号和节拍单位生成小节线、拍点和细分网格。
- MusicAnalysis：提供调性、节奏和其他与输入格式无关的分析结果。
- ScorePlaybackDom：保存播放域需要的谱面和源事件关系。

MusicDocument 在导入阶段可变；进入 PlaybackModel、VisualChart 等消费者后通常以 shared_ptr<const MusicDocument> 共享。读取完成后，消费者不应再修改文档，否则会破坏时间索引和投影结果的一致性。

### 4.3 domain/playback：事件、传输和音频抽象

目录：src/domain/playback

| 对象 | 职责 |
| --- | --- |
| PlaybackModel | 从 MusicDocument 生成每轨道事件、全局事件、快照、事件索引和 PlaybackTimeline |
| PlaybackEvent | 表示 NoteOn、NoteOff、Program Change、控制器、弯音、压力等事件 |
| PlaybackEventMap | 按时间和协议优先级排序事件，并提供 lowerBound |
| PlaybackStateSnapshot | 保存某时间点的通道状态和活跃长音，服务 seek 恢复 |
| PlaybackEventScheduler | 根据当前位置收集下一段待发送事件 |
| PlaybackSession | 在播放线程维护 transport 状态、2ms 检查周期、事件 generation 和节拍器 |
| PlaybackController | 在 GUI 侧管理会话线程，节流位置样本并转发操作 |
| PlaybackClock / PlaybackPlayHead | 处理单调时钟、音频时钟和播放速率 |
| IPlaybackAudioService | 音频后端的最小抽象，屏蔽 FluidSynth 类型 |
| MetronomeTimeline / MetronomeScheduler | 根据拍号、tempo 和重复段生成点击事件 |

该层不依赖 Qt Widgets，不知道主窗口如何布局。PlaybackSession 可以被 CLI、测试或其他界面通过 PlaybackController 使用。

### 4.4 domain/visualization：可视化投影和窗口查询

目录：src/domain/visualization

| 对象 | 职责 |
| --- | --- |
| PlaybackVisualizationProjector | 把 MusicDocument 和 PlaybackSegment 投影为 VisualChart |
| VisualChart | 不可变视觉模型，包含视觉音符、轨道、网格、标记、歌词、tempo、调号和音域 |
| VisualNote | 保存 startUs、keyEndUs、audibleEndUs、颜色语义、鼓组 lane、简谱信息和稳定实例 ID |
| VisibleNoteIndex | 对音符时间区间建立带 subtree max-end 的索引，支持窗口查询 |
| VisibleNoteWindowCache | 复用相邻播放窗口的候选音符，seek 或越界时重新查询 |
| ActiveNoteLookup | 以固定大小查找表维护当前激活音符和鼓组状态 |
| NoteOverlapLayout | 为重叠音符分配绘制 lane |
| PlaybackSceneState | 保存主题、传输状态、可见窗口和当前候选音符 |

投影阶段会处理反复段、踏板延音、连音合并、打击乐 lane、简谱 degree、音符重叠布局和音域保护。VisualChart 是 Qt 绘制、Vulkan 绘制和 CLI 截图共同使用的中间模型。

### 4.5 domain/settings：设置值对象和规范化

目录：src/domain/settings

该目录保存 PlayerSettings 及相关枚举：

- GraphicsMode：VulkanExperimental 或 Traditional；
- ThemeMode：深色或浅色；
- NoteColorMode：普通或鲜明配色；
- VisualEffectLevel：无、低、中、高；
- TitleBarMode：原生或自定义标题栏；
- BackgroundMode：无背景、图片、视频；设置页将视频标记为实验功能；
- BackgroundImageAlignment：图片和视频共用的适配方式；
- 播放速率、可视化刷新率、节拍器和简谱条开关。

领域设置函数提供枚举规范化和持久化值转换，SettingsService 负责运行时偏好。涉及旧配置键是否存在的迁移在 QSettingsStore 中完成，例如 schema 15 的背景模式迁移；UI 不自行解释旧配置。

### 4.6 infrastructure/readers、midi、musicxml：输入适配

目录：

- src/infrastructure/readers
- src/infrastructure/midi
- src/infrastructure/musicxml

MusicReaderRegistry 根据文件后缀选择 IMusicReader。当前注册：

1. MusicXmlReaderAdapter：处理 MusicXML 相关后缀，委托 MusicXmlReader；
2. MidiReaderAdapter：处理 mid、midi、kar，委托 MidiReader。

MIDI 读取链路是：

~~~text
MidiFileParser
  -> MidiParsedFile
  -> MidiNormalizer
  -> MidiDocumentBuilder
  -> MusicDocument
~~~

Parser 负责字节级格式读取、变长整数、轨道和事件解析；Normalizer 负责把 running status、时间基准、格式 0/1/2、控制器和音符配对整理为统一中间数据；Builder 负责构造轨道、tempo、拍号、program、鼓组和文档级元数据。

MIDI format 2 的各个独立序列会被记录到 sequenceStarts，并在统一时间线上按序衔接；每个序列的 tempo 和节拍状态从自己的起点重新建立。SMPTE division 会使用绝对时间换算，并标记文档没有可用于节拍器的音乐时间基准。

MusicXML Reader 通过 Qt XML 读取 partwise 文档，并把 timewise 结构转换为统一的 part/measure 处理路径。它重点提取播放所需的音符、休止、voice、staff、tie、tempo、拍号、调号、歌词、标记和踏板相关信息。

Reader 返回 ReadResult。读取失败时，Application 层只发布可读错误，不创建新的播放会话。

### 4.7 infrastructure/audio：FluidSynth 和线程边界

目录：src/infrastructure/audio

| 对象 | 职责 |
| --- | --- |
| FluidSynthEngine | 动态解析 FluidSynth 符号，创建 synth、音频驱动和 SoundFont，支持实时与离线渲染 |
| FluidSynthAudioService | 把 IPlaybackAudioService 调用翻译为 FluidSynth API |
| ThreadedPlaybackAudioService | 为音频后端建立工作线程，串行执行加载、控制和事件提交 |
| SoundFontInspector | 在启动实际合成前检查 SF2/SF3 容器和可读性 |

FluidSynthEngine 使用 QLibrary 获取函数指针，因此核心程序在没有 FluidSynth DLL 时仍可以启动并显示可用性错误；实际播放、音频测试和导出需要有效的运行时 DLL 与 SoundFont。

节拍器使用独立的内置资源和合成器/声部池，避免占用歌曲的 MIDI 通道。切换音源、暂停、停止和 seek 时，播放会话负责停止节拍器并重新定位节拍器调度器。

### 4.8 infrastructure/media、encoding、settings、resources

| 目录 | 职责 |
| --- | --- |
| infrastructure/media | VideoFrameReader，进程内 FFmpeg 视频解码、轨道时长与音视频起点元数据 |
| infrastructure/encoding | FFmpeg 进程、视频编码器 worker、原声对齐与混音、音频文件编码器、FFmpeg 探测 |
| infrastructure/settings | QSettingsStore，使用 Qt 用户级 INI 持久化 |
| infrastructure/resources | 项目音频设置和轨道音源配置模型 |

视频导出和原声混音通过 QProcess 调用外部 FFmpeg；VideoFrameReader 在进程内调用 FFmpeg 库，向调用者返回 VideoMetadata 和 QImage，不把 FFmpeg 类型传入领域层。QSettingsStore 负责存取、旧键迁移和读取警告，SettingsService 负责运行时规范化、状态发布和保存编排。

### 4.9 presentation：界面、渲染和用户操作

目录：src/presentation

主要职责：

- MainWindow：主窗口、文件打开、播放控制、进度条、元数据、设置和导出入口；
- SettingsDialog、SoundFontSetup：设置编辑和首次音源配置；
- ExportDialog、AudioExportPage、VideoExportPage：导出参数输入和可用性提示；
- FallingNotesView：传统 Qt/QPainter 绘制入口，处理背景、钢琴键、网格、音符和播放状态；
- VideoBackgroundController：接收音乐时间，调度异步视频帧解码，并在启用原声时协调 Qt Multimedia 播放器；
- FallingNotesVulkanWindow：Vulkan 实时窗口，使用共享的 VisualChart 和场景布局；
- FallingNotesRenderer、SceneLayoutEngine：抽取可复用的几何和绘制准备逻辑；
- VulkanOffscreenRenderer：视频导出的离屏 Vulkan 设备、帧槽、readback 和 fence 管理；
- theme、windowchrome、transport：主题、标题栏、播放速率和通用界面组件。

Presentation 层可以订阅 Application 和 PlaybackController 的信号，但不应直接改变 PlaybackModel、MusicDocument 或 FluidSynthEngine 的内部状态。

## 5. 进程启动与对象生命周期

### 5.1 GUI 启动

入口：src/main.cpp。

启动顺序如下：

1. 根据可执行文件目录加入打包的 Qt platforms 路径；
2. 创建 QApplication，设置应用名、组织名和窗口图标；
3. 创建 PlayerApplicationService；
4. 创建 QSettingsStore 和 SettingsService，读取并规范化用户设置；
5. 创建 FfmpegService 和 ThemeController；
6. 把刷新率、图形模式和音源选择等设置通过 signal 连接到应用服务；
7. 创建 MainWindow 并显示；
8. 创建 SoundFontSetup，异步完成首次音源配置；
9. 如果命令行带有输入文件，则在 SoundFontSetup 完成后调用 openFile；
10. 进入 QApplication::exec。

SoundFontSetup 的完成信号用于避免尚未完成音源引导时就开始打开乐曲。没有有效音源时，用户可以跳过配置并先浏览或渲染乐曲；播放前由 PlayerApplicationService 再次验证音源路径。

### 5.2 CLI 启动

入口：src/cli/main.cpp。

CLI 根据命令选择 QCoreApplication 或 QGuiApplication：

- 普通解析、MIDI 测试、音频导出使用 QCoreApplication；
- render-test 需要 QImage 和 QPainter，因此使用 QGuiApplication；
- 不创建 MainWindow，不读取 GUI 用户设置；
- 注册与 GUI 相同的 MusicXmlReaderAdapter 和 MidiReaderAdapter。

CLI 的输入文件单参数模式执行 reader smoke test；--midi-test 直接验证 MIDI Reader；--audio-test 验证 FluidSynth 播放与换音源；--export-mp3/--export-wav 执行离线音频导出；--render-test 生成指定位置的可视化 PNG；--video-background-test 通过进程内解码器检查视频首尾帧与反向 seek。

### 5.3 对象所有权

| 对象 | 所有者 | 主要线程 |
| --- | --- | --- |
| MainWindow、SettingsService、FfmpegService | main 函数中的 Qt 父子对象或栈对象 | GUI 线程 |
| PlayerApplicationService | main 函数创建，Qt 事件循环驱动 | GUI 线程 |
| PlaybackController | PlayerApplicationService 的 unique_ptr | GUI 线程 |
| PlaybackSession | PlaybackController 的 unique_ptr，创建后 moveToThread | PlaybackThread |
| ThreadedPlaybackAudioService | PlaybackSession 的 unique_ptr | 通过内部 Worker 串行访问 |
| MusicDocument、PlaybackModel、VisualChart | shared_ptr，在加载完成后只读共享 | 构建线程完成后多线程只读 |
| QFutureWatcher | PlayerApplicationService 的 QObject 子对象 | GUI 线程接收完成信号 |
| VideoBackgroundController、QMediaPlayer、QAudioOutput | FallingNotesView 持有控制器，控制器拥有原声播放器与输出对象 | GUI 线程协调，Qt Multimedia 管理内部音频后端 |
| VideoFrameReader | 实时控制器的共享 Worker 或离屏 renderer 独占 | 实时由 QtConcurrent 任务串行访问；导出由导出 worker 访问 |
| VideoExportService / AudioExportService | 调用方拥有，任务期间保持输入文档和临时资源 | GUI 或导出 worker，取决于调用方 |

析构 PlaybackController 时会先停止位置定时器，再通过 BlockingQueuedConnection 停止会话，退出并等待播放线程，最后释放会话。这样可以保证音频工作线程不再访问即将销毁的对象。

视频解码任务只捕获 Worker 和不可变请求参数，不访问 GUI 对象。控制器析构或换源时设置取消标志；Worker 由任务持有直到结束，源代次与 Worker 身份共同阻止过期结果发布。原声换源会断开并释放旧播放器，避免旧源回调影响新源。

## 6. 打开音乐文件的端到端流程

### 6.1 GUI 文件打开

~~~mermaid
sequenceDiagram
    participant User as 用户
    participant Window as MainWindow
    participant App as PlayerApplicationService
    participant Registry as MusicReaderRegistry
    participant Worker as QtConcurrent 任务
    participant Reader as Parser/Normalizer/Builder
    participant Projector as VisualizationProjector
    participant Controller as PlaybackController
    participant Session as PlaybackSession

    User->>Window: 选择 MIDI/KAR/MusicXML
    Window->>App: openFile(path)
    App->>Registry: find(suffix)
    App->>Worker: 异步 read + project
    Worker->>Reader: 解析并构建 MusicDocument
    Worker->>Projector: 构建 VisualChart
    Worker-->>App: LoadedProject
    App->>App: 校验 generation
    App->>Controller: 创建并设置 PlaybackSession
    Controller->>Session: moveToThread(PlaybackThread)
    App-->>Window: visualizationReady / documentLoaded
    Window-->>User: 显示音符和播放控件
~~~

PlayerApplicationService::openFile 的关键行为：

1. 音源加载期间拒绝打开新文件，避免替换音源事务与文档事务交错；
2. 通过后缀查找 reader，未找到时立即发布错误；
3. 递增 m_loadGeneration；
4. 在 QtConcurrent 任务中执行 reader->read 和 PlaybackVisualizationProjector::project；
5. 完成回调检查任务 generation 是否仍是当前 generation；
6. 旧任务结果被丢弃，不会覆盖用户后来选择的新文件；
7. 只有读取成功、VisualChart 有效且 PlaybackController::setDocument 成功后，才替换当前控制器和文档；
8. 若已保存的外部音源加载失败，保留已解析的乐曲，但清空不可用的音源路径并向 UI 报告错误。

### 6.2 Reader 选择和错误边界

后缀匹配由 MusicReaderRegistry 完成，解析器不依赖 UI。Reader 负责格式错误和语义转换，Application Service 负责把错误转成用户可见状态。错误不会通过异常跨越 Qt signal/slot 边界，通常使用 ReadResult、QString* error 和状态信号传播。

### 6.3 MusicDocument 定稿

Builder 完成后应满足：

- 至少有一个有效轨道；
- duration 大于 0；
- 音符起止 tick 在文档范围内；
- tempo、拍号和调号已经归一化；
- 文档级 measures、markers、lyrics 和 key context 可供后续投影使用；
- tempo map 在 PlaybackTimeline 构建前完成惰性重建。

## 7. 音乐时间模型

### 7.1 Tick、PPQ 和微秒

MusicDocument 使用固定 PPQ=480 的音乐 tick。tick 表示拍内相对位置，微秒表示播放和渲染所用的绝对时间。所有跨线程 transport、PlaybackEvent 和 VisualNote 时间字段均使用 qint64 微秒。

核心换算：

- tickToMicroseconds：按 tempo map 把源 tick 转为微秒；
- microsecondsToTick：通过二分查找把微秒反向映射为 tick；
- playbackTickToMicroseconds：先经过重复展开后的输出 tick，再累计各 PlaybackSegment 的真实源时间；
- PlaybackTimeline：预计算 segment index，避免实时线程重复线性扫描。

没有显式 tempo 时使用 120 BPM。SMPTE 时间基准可以使用合成 tempo 完成绝对时间换算，但 m_hasMusicalTimebase 会阻止把该合成时间误当作节拍网格。

### 7.2 Tempo map

MusicDocument::rebuildTempoMap 按 tick 和 sequence 排序 TempoChange，逐段累计微秒。相同 tick 的 tempo 变更以序列顺序处理，最后一个值成为该时间点的有效 tempo。PlaybackTimeline 在被多个线程读取前主动完成 tempo map 构建，使播放线程只读稳定的数据。

### 7.3 重复展开

RepeatList::build 根据 Measure 的 repeatStart、repeatEnd、endingNumber、segno、coda、daCapo、dalSegno、toCoda 和 fine 字段生成 PlaybackSegment。

每个段记录：

- sourceStart/sourceEnd：源文档区间；
- outputStart：展开后输出时间线的起点；
- repeatPass：当前重复次数；
- sourceMeasureIndex：用于视觉标记和调试。

展开过程设置最大步数，防止损坏或互相矛盾的跳转标记造成无限循环。没有小节信息时，整个文档作为单一段落处理。

### 7.4 音符持续时间

NoteEvent::duration 表示键释放时间；sustainEnd 或控制器事件可以表示踏板保持后的可听结束时间。播放事件和视觉投影都必须区分：

- key end：按键释放，影响 NoteOff；
- audible end：踏板或延音导致的声音结束，影响视觉尾部和重叠查询。

## 8. 播放事件和传输流程

### 8.1 PlaybackModel 构建

PlaybackModel 接收只读 MusicDocument，构建：

1. 每个轨道的 PlaybackData；
2. 主事件流和显式 off 事件流；
3. 全局事件流及其 PlaybackEventIndex；
4. 记录 Program、Bank Select、控制器、弯音、压力和活跃长音的 PlaybackStateSnapshot；
5. 共享的 PlaybackTimeline 和 ScorePlaybackDom。

事件时间统一为 playback timeline 的微秒。反复段中的同一个源音符可以产生多个输出实例，但每个输出事件保留稳定的 sequence/noteId 关系。

### 8.2 同一时间点的事件顺序

PlaybackEventMap::finalize 按以下优先级排序同一 timestamp 的事件：

1. NoteOff 和 AllNotesOff；
2. Bank Select（控制器 0 和 32）；
3. Program Change；
4. 其他控制器、弯音、压力和 NoteExpression；
5. NoteOn。

该顺序满足 MIDI 合成器的协议约束：Bank Select 必须先于 Program Change，旧音符释放应先于同一时间点的新音符建立。

### 8.3 PlaybackSession 的播放循环

PlaybackSession 在 PlaybackThread 中创建一个 Qt PreciseTimer，周期为 2ms。onTimer 的主要步骤是：

1. 若后端有音频时钟，先读取后端位置并修正 playhead；
2. 根据 playhead 计算当前位置；
3. 从 PlaybackEventScheduler 收集当前位置到 dispatchUntilUs 的事件；
4. 定时事件后端预留约 30ms 的发送窗口，非定时后端只发送当前位置以前的事件；
5. 以当前 event generation 批量提交到 IPlaybackAudioService；
6. 调度到文档末尾时执行 stop；
7. 若节拍器可用，提交当前到期的点击；
8. 发布当前位置样本。

PlaybackSession 是播放时间的权威来源。PlaybackController 的位置定时器只负责把高频样本压缩到 UI 所需的刷新率。

### 8.4 状态转换

常见状态为 Empty、Ready、Playing、Paused、Stopped、Error。

- play：启动音频后端，设置 transport 位置，重建目标位置的音频状态，然后启动 playhead 和 2ms timer；
- pause：读取当前位置，停止 timer 和节拍器，flush 活跃音符，暂停音频后端并保留精确位置；
- stop：停止 timer，递增 generation，flush 音符和节拍器，重置音频、playhead、scheduler 和位置；
- seek：停止当前调度，递增 generation，flush 旧事件，调整音频位置、playhead 和 scheduler；若之前在播放，则从新位置恢复；
- Error：音频启动、音源切换或恢复失败时进入错误状态并发布 errorOccurred。

自然播放结束同样进入 Stopped，但保留音乐终点位置；再次 play 从零开始。显式 stop 才归零，因此 UI 不能仅凭 Stopped 判断“停止”按钮不可用：终点或停态非零 seek 位置仍可点击停止。视频背景据此保留终帧或重置。

### 8.5 generation 防止旧事件泄漏

每次 seek、stop、暂停恢复边界或音源切换都可能让已经排队的事件失去意义。PlaybackSession 递增 m_eventGeneration，同时通知 scheduler 和 audio service。ThreadedPlaybackAudioService 及其 worker 在提交和执行事件时检查 generation，从而忽略旧 transport 事务残留的事件。

generation 只用于丢弃过期事件；它不代替位置本身。位置仍由 playhead、audio clock 和 scheduler 游标共同维护。

### 8.6 seek 后恢复状态

PlaybackSession::rebuildAudioState 的恢复顺序：

1. 在全局事件索引中找到目标时间点前的事件边界；
2. 选择不晚于目标位置的最近 PlaybackStateSnapshot；
3. 先恢复 Bank Select，再恢复 Program Change；
4. 按 RPN、NRPN、Data Entry 等协议顺序恢复控制器；
5. 恢复 Pitch Bend、Channel Pressure 和 Poly Pressure；
6. 扫描快照之后到目标位置之间的状态事件；
7. 计算仍然活跃的长音，并从目标位置重新发送剩余时长；
8. 对已经释放键但仍由踏板保持的音符补发 NoteOn 后立即补发 NoteOff 语义；
9. 使用当前 generation 一次性提交恢复批次。

该流程保证 seek、暂停恢复和切换音源后，通道状态与持续音符不会因为只重放“当前位置之后”的事件而丢失。

### 8.7 PlaybackController 的线程协调

PlaybackController 运行在 GUI 线程，持有 PlaybackThread 和 PlaybackSession：

- play、pause、stop 使用 queued invocation；
- seek 使用 BlockingQueuedConnection，等待会话完成整个 transport 事务后再立即刷新 UI 端点；
- 同一会话的 positionChanged 使用 DirectConnection 写入 PlaybackPositionThrottler；
- GUI 侧 QChronoTimer 按设置刷新率取出最新样本；
- 会话替换时停止并等待旧线程，避免旧会话的 queued signal 覆盖新文档状态。

控制器信号是 UI 的稳定边界。UI 不应直接调用 PlaybackSession 的私有状态或依赖 2ms timer 的频率。

## 9. 音频后端和 SoundFont

### 9.1 实时音频

实时音频链路为：

~~~text
PlaybackSession
  -> IPlaybackAudioService
  -> ThreadedPlaybackAudioService
  -> FluidSynthAudioService
  -> FluidSynthEngine
  -> FluidSynth audio driver / callback
  -> 系统音频设备
~~~

ThreadedPlaybackAudioService 把调用转移到内部 worker，确保 FluidSynth 对象不被多个线程并发操作。PlaybackSession 只看到抽象能力，例如 timed events、playback rate、metronome 和 audio clock。

视频原声使用独立的 QMediaPlayer/QAudioOutput，由系统混音器与 FluidSynth 合并。它不替代 IPlaybackAudioService，也不成为音乐时钟；控制器根据音乐位置校正原声的播放位置、倍率与状态。默认屏蔽原声时不创建播放器。

### 9.2 音源加载和热切换

用户选择音源时，PlayerApplicationService 先检查路径存在、可读、扩展名为 sf2 或 sf3，并通过 SoundFontInspector 检查容器。已有会话时，PlaybackSession 执行以下事务：

1. 记录当前是否播放以及当前位置；
2. 停止 timer 和节拍器；
3. 递增 event generation 并 flush 当前音符；
4. 暂停旧后端；
5. 加载新 SoundFont；
6. 为所有轨道恢复 program、bank 和轨道设置；
7. 重新准备节拍器；
8. 设置目标 transport 位置并重建音频状态；
9. 如果之前正在播放，恢复 playhead 和 timer；
10. 失败时尽可能恢复原音源和原播放状态。

如果会话尚未创建，音源只做校验并保存路径；真正的 FluidSynth 初始化在打开文档后进行。

### 9.3 FluidSynth 动态加载

FluidSynthEngine 通过 QLibrary 动态解析 settings、synth、audio driver、SoundFont、MIDI 事件和离线 process 所需函数。它捕获 FluidSynth 日志以生成可读诊断，并在析构时按 driver、synth、settings、library 的依赖顺序释放资源。

## 10. 可视化和双渲染后端

### 10.1 VisualChart 投影

PlaybackVisualizationProjector 将每个 PlaybackSegment 中的源音符投影到输出时间线，生成 VisualNote：

- startUs：音符开始；
- keyEndUs：按键释放；
- audibleEndUs：踏板处理后的可听结束；
- repeatPass/sourceMeasureIndex：反复上下文；
- trackIndex/pitch/channel：绘制和交互索引；
- drumLane：打击乐纵向通道；
- simplifiedLabel：简谱标签；
- flags：连音、演奏法、打击乐等视觉语义；
- instanceId：稳定的可视化实例 ID。

投影器还生成 tempo、调号、拍号、网格线、歌词和标记，并为重叠音符分配绘制 lane。连音可以按选项合并，避免视觉上重复绘制同一个持续音符。

### 10.2 可见窗口查询

渲染帧根据 transportPositionUs 和 lookAheadUs 计算时间窗口。VisibleNoteIndex 对音符区间建立平衡索引，节点保存子树最大结束时间，因此窗口查询约为 O(log N + K)，K 为候选音符数量。

VisibleNoteWindowCache 在窗口小步移动且仍位于上次查询范围内时复用候选索引；seek、窗口跳跃或 chart 替换时清空缓存。ActiveNoteLookup 使用固定范围的音高查找减少每帧线性搜索。

### 10.3 共享场景数据

PlaybackSceneState 保存：

- VisualChart；
- transportPositionUs、durationUs、transportState；
- themeMode、noteColorMode；
- lookAheadUs 和 visibleWindow；
- 当前候选音符索引。

SceneLayoutEngine 根据窗口尺寸、chart 音域、钢琴键和简谱条配置生成布局。FallingNotesRenderer 负责与 QPainter 相关的传统绘制准备，Vulkan 路径复用同一套布局、材质语义和播放状态。

### 10.4 传统 Qt 绘制

FallingNotesView 使用 QPainter 绘制背景、网格、钢琴键、音符、歌词、标记和播放指示。背景图片通过 BackgroundImageLoader 异步加载，视频帧由 VideoBackgroundController 提供；两者共用 BackgroundImagePlacement、适配方式、不透明度和可读性遮罩。静态钢琴键、黑键分隔、文字布局和部分音符材质使用缓存，降低高密度曲目的 CPU 绘制开销。

### 10.5 Vulkan 实时窗口

FallingNotesVulkanWindow 负责 QVulkanWindow 生命周期、交换链相关资源和当前帧绘制。VulkanPipeline、VulkanScene、NoteMaterial 和 shader 负责图形管线、动态音符批次和流光玻璃效果。

Vulkan 模式与传统模式共享：

- VisualChart；
- SceneLayoutEngine；
- PlaybackSceneState；
- 音符颜色和材质语义；
- 主题、图片或视频背景帧、背景布局和播放位置。

只有绘制后端、GPU 资源和特效实现不同。直接使用 CMake 且未开启 MIDI_PLAY_REQUIRE_VULKAN 时，构建能力不完整允许回退传统版本；SOP 的 Vulkan 构建要求依赖齐全，否则失败。运行时初始化失败时，设置和窗口逻辑会回退到传统模式。

### 10.6 Vulkan 离屏渲染

VulkanOffscreenRenderer 用于视频导出，不依赖 QVulkanWindow 的可见窗口。它创建独立的 instance/device/queue/render pass/framebuffer，并为多个 frame-in-flight 槽分配命令缓冲、颜色图像、readback buffer 和 fence。

离屏渲染分为两个阶段：

1. beginRender：准备场景、上传当前帧数据、记录并提交 GPU 命令，返回 render ticket；
2. completeRender：等待对应 slot 的 fence，把 host-visible readback buffer 拷贝为 QImage。

slot 拥有该帧提交所需的缓冲和同步对象，因此下一帧可以准备另一个 slot；只有对应 fence 完成后才读取和复用该 slot。取消标志会在场景准备、提交等待和 readback 阶段检查。

视频背景使用独立 VideoFrameReader，按输出帧对应的音乐位置读取背景。动态背景纹理、staging 和 descriptor 按三个 slot 隔离，上传当前背景不再等待其他在途槽；静态图片继续共享纹理，共享文字图集变化仍等待在途工作完成。这消除了背景上传的跨槽等待，不代表整个导出吞吐必然同比提高。

### 10.7 视频背景（实验）的时间线与调度

`backgroundVideoPosition` 以整数微秒映射音乐位置到视频周期：播放中按视频时长取余；音乐终点及导出尾音取终点左侧时刻，避免恰好在循环边界时闪回首帧。音乐变速通过相同的音乐位置映射自然传递给背景，视频自身时钟不驱动画面。

VideoFrameReader 仅缓存当前帧和下一帧，顺序播放复用上下文；随机定位先 seek 到关键帧，再按 PTS 解码到包含目标时刻的帧。seek 目标向下取整到流时间基，避免提前显示未来帧。视频轨缺少 duration 时扫描该轨 packet PTS，不能用可能被长音轨延长的容器总时长代替。第一音轨时长缺失也需扫描；两条轨均缺失时长会分别扫描，默认静音仍读取这些元数据，大文件首次打开可能增加 I/O。

VideoBackgroundController 最多保留一个运行任务和一个可覆盖的最新目标。持续拖动只更新待处理目标，让在途帧完成并发布后继续解码最新位置，避免不断取消导致预览饥饿；切换源才取消并隔离旧结果。MainWindow 拖动期间让音符与背景使用同一个预览位置，视频原声静音；松开后提交一次音乐 seek，保留预览直到收到精确位置确认。

原声采用第一音轨，与视频首帧的源时间对齐。有效音频区间外静音，下一循环按相同区间恢复；音乐结束立即停止原声。是否播放由音乐状态与音轨区间决定，不用迟到的 EndOfMedia 标记循环。Qt 原声位置在 seek、循环或偏差超过 120 ms 时重锚，不能宣称采样级同步。详细支持范围与测试素材见 [视频背景说明](video-background.md)。

## 11. 设置、主题和外部资源

### 11.1 设置加载

启动时 QSettingsStore 读取用户级 INI 并处理旧键迁移和读取警告，SettingsService 继续规范化和发布设置：

- 刷新率、播放速率和枚举值规范化；
- 音源、FFmpeg 和背景图片/视频路径转为绝对路径；
- 背景不透明度限制在 0 到 100；
- schema 15 的合法 BackgroundMode 优先，包括显式 None；缺失或非法值回退旧图片开关，非法值额外报告 warning；
- 传统图形模式强制关闭 Vulkan 特效；
- 传统构建读取 Vulkan 版本遗留设置时保留偏好，但不向当前界面暴露启用状态。

加载修复产生 warning 时，SettingsService 通过 settingsLoadWarning 通知设置窗口；成功持久化后，SettingsService 清除旧 warning。

### 11.2 设置变更

SettingsDialog 只通过 SettingsService 的 setter 修改设置。每个 setter：

1. 规范化输入；
2. 检查值是否变化；
3. 更新 PlayerSettings；
4. 发出对应的 changed signal；
5. 调用 QSettingsStore 保存。

保存失败通过 settingsSaveFailed 发布，UI 可以显示错误但保留内存中的当前值。

### 11.3 图形模式和特效

图形模式由 SettingsService、PlayerApplicationService 和 MainWindow 协同：

- 新用户首次启动优先尝试 Vulkan；
- Vulkan 构建或运行不可用时使用 Traditional；
- Traditional 模式隐藏或禁用流光特效等级，并把 enabled 规范化为 false；
- Vulkan 模式下保存 enabled 和 VisualEffectLevel；
- 切换模式不会丢失未来可用的 Vulkan 偏好，但当前后端不会应用不支持的特效。

### 11.4 主题和背景

ThemeController 根据 ThemeMode 更新 Qt 样式和图标。SettingsService 分别保留图片与视频路径，BackgroundMode 决定当前启用的资源；禁用背景或切换模式不会删除另一路径。FallingNotesView 协调图片异步加载或视频解码，并把同一 QImage 交给传统 Qt 和 Vulkan 后端。两种资源共用六种适配方式、不透明度、可读性遮罩和 256–4096 px 最大边长，默认 2048 px。

设置页显示“无背景”“图片背景”“视频背景（实验）”，视频模式额外显示默认开启的“屏蔽视频原声”。原始图片和视频保持为外部本地资源，路径失效时保留设置并报告错误，不复制或嵌入原文件。

## 12. 音频导出流程

### 12.1 WAV/MP3 导出

入口：AudioExportService::exportDocument。

流程：

~~~mermaid
flowchart LR
    A[MusicDocument] --> B[Export options 校验]
    B --> C[OfflineAudioRenderer]
    C --> D[FluidSynthEngine.loadOffline]
    D --> E[PlaybackModel / 事件调度]
    E --> F[PCM float blocks]
    F --> G[AudioFileEncoder]
    G --> H[临时输出文件]
    H --> I[QSaveFile 提交]
~~~

OfflineAudioRenderer 使用导出采样率创建 FluidSynth 离线 synth，按固定帧块推进播放事件并将左右声道写入 sink。它会处理文档重复段、尾音、节拍器和取消标志。

AudioFileEncoder 根据 WAV 或 MP3 目标写入格式。导出完成前使用临时路径；取消或失败时不替换已有目标文件，成功后再安全提交。

### 12.2 音频导出边界

导出参数包括 SoundFont 路径、采样率、MP3 比特率、尾音时长和节拍器开关。参数不合法、音源不可读、FluidSynth 初始化失败、编码器失败或取消都会返回带状态的 AudioExportResult。

## 13. 视频导出流程

### 13.1 编排顺序

入口：VideoExportService::exportDocument。

视频导出需要 Vulkan 图形设备和可用的外部 FFmpeg 命令行工具。仅在包含乐曲音频时需要有效 SoundFont；关闭乐曲音频可输出静音视频，或仅包含已启用的视频原声。主要阶段：

1. 校验文档、尺寸、帧率、输出路径和 FFmpeg；
2. 构造 ExportTimeline，确定视频帧、音乐位置和音频采样帧的映射；
3. 初始化 VulkanOffscreenRenderer，视频背景使用导出任务自己的解码器；
4. 按选项调用 OfflineAudioRenderer 生成乐曲 PCM；若启用视频原声，再对齐其起点、循环和变速，输出原声 PCM 或与乐曲混音；
5. 创建 FfmpegVideoEncoderWorker，准备有限数量的 QImage 帧槽；
6. 用 beginRender 预提交若干帧；
7. 完成最早的 render ticket，获取 QImage 并提交到 FFmpeg；
8. 继续交替提交和完成帧，形成 GPU、CPU readback 和编码器的流水线；
9. FFmpeg 关闭输入并完成封装；
10. 有 ffprobe 时校验输出帧数、尺寸、帧率和音频流，并使用 FFmpeg 完整解码检查成片；
11. 通过 QSaveFile 把临时输出提交为最终文件；
12. 记录 Vulkan、readback、编码器等待和帧数诊断。

### 13.2 帧时间映射

ExportTimeline 使用固定 fps 产生视频帧位置，使用固定音频采样率产生 PCM 帧。视频第 n 帧对应的音乐位置由同一条 timeline 计算，避免视频帧数和音频长度因独立取整而漂移。

背景画面复用 backgroundVideoPosition。原声处理先保留源时间戳、减去视频轨起点并填充音轨空缺，生成一个视频周期的 PCM，再循环、变速并截到音乐结束，尾音阶段补静音。与乐曲同时输出时使用 `amix normalize=1` 归一化混音；这不等于目标响度归一化，且实时系统混音的音量可能不同。独立 MP3/WAV 音频导出不包含视频原声。

### 13.3 背压、取消和临时文件

FfmpegVideoEncoderWorker 使用有界帧队列和条件变量。生产者在队列满时等待，消费者编码完成后归还 frame slot，从而限制内存占用。取消时会终止等待、停止 FFmpeg 进程、清理临时文件，并保留原目标文件。

视频导出使用 QSaveFile 且关闭 direct write fallback。只有所有帧编码并通过完整性检查后才提交目标文件；ffprobe 缺失时仍执行 FFmpeg 完整解码校验，有 ffprobe 但校验失败时不会提交。

## 14. CLI 操作流程

以下语法中的尖括号表示必填参数，方括号表示可选参数，不应原样输入终端。可直接替换路径运行的 PowerShell 示例见 [README 命令行验证](../README.md#命令行验证)。

### 14.1 读取验证

~~~text
midi_play_cli <xml|musicxml|mid|midi|kar file>
midi_play_cli --midi-test <MIDI file>
~~~

两种命令都注册 reader 并构建 MusicDocument。普通单文件模式输出标题、轨道数和 duration_us；--midi-test 专门验证 MidiReader。当前 ReaderRegistry 未注册压缩 MusicXML 的 mxl 后缀，CLI 帮助中的历史提示需要以实际支持的后缀为准。

### 14.2 音频测试

~~~text
midi_play_cli --audio-test <sf2|sf3 file> [replacement SoundFont]
~~~

该命令创建 FluidSynthEngine，加载并配置通道 0，发送一个短 NoteOn/NoteOff，并可在播放期间切换到第二个 SoundFont。它用于确认动态库、音源、音频设备和热切换链路。

### 14.3 音频导出

~~~text
midi_play_cli --export-wav <music file> <output file> --soundfont <sf2|sf3>
midi_play_cli --export-mp3 <music file> <output file> --soundfont <sf2|sf3>
~~~

可选参数包括 --bitrate、--sample-rate、--tail-ms 和 --metronome。CLI 先通过 reader 构建文档，再调用 AudioExportService，不创建播放会话。

### 14.4 离屏截图

~~~text
midi_play_cli --render-test <music file> <output.png> [position_us [width [height]]] [--theme dark|light] [--note-colors normal|vivid]
~~~

截图流程直接构建 VisualChart、PlaybackSceneState、VisibleNoteIndex、SceneLayoutEngine 和 FallingNotesRenderer。该命令使用 QPainter，固定使用显式主题和音符颜色参数，不读取桌面应用设置，便于重复比较。

### 14.5 视频背景解码检查

~~~text
midi_play_cli --video-background-test <video file>
~~~

通过 VideoFrameReader 读取首帧和终帧，再反向 seek 到零并比较首帧。该检查使用进程内 FFmpeg 库，不需要外部命令行工具；SOP 在清理开发 SDK PATH 后对已部署 CLI 执行此命令。它不验证 Qt 原声音频设备或 GPU 绘制。

## 15. 构建、部署和运行时依赖

### 15.1 配置和构建

CMakePresets.json 提供 Windows MSVC/Ninja 基线。scripts/Build-Windows.ps1 在构建前检查：

- Visual Studio 和 Windows SDK；
- Qt 目录及组件，包括 Multimedia；
- Vulkan SDK、Vulkan loader 和 glslangValidator；
- vcpkg 依赖；
- 可选 SoundFont、FFmpeg 和音频设备测试参数。

MIDI_PLAY_ENABLE_VULKAN 控制是否请求 Vulkan，MIDI_PLAY_REQUIRE_VULKAN 控制缺失 Vulkan 时是否把 warning 升级为配置失败。未启用 Vulkan 时仍然生成传统 Qt 播放器和 CLI。

手动构建需通过 CMAKE_MODULE_PATH 指向 vcpkg 安装目录下的 share/ffmpeg，使用其 FindFFMPEG.cmake；Windows 路径传入 CMake 时统一为正斜杠。完整命令见 [README 手动开发构建](../README.md#手动开发构建)。SOP 已提供该路径，并检查 Multimedia 和解码 DLL。

### 15.2 发行包

部署脚本复制：

- midi_play.exe 和 midi_play_cli.exe；
- Qt Core、Gui、Widgets、Xml、Multimedia 及部署工具解析到的传递依赖；
- platforms/qwindows.dll；
- FluidSynth DLL 及其非系统依赖；
- LAME 和其他编码依赖；
- 进程内 FFmpeg 解码 DLL，以及 Qt Multimedia 所需的插件和媒体依赖；
- 必需的 Qt 资源和插件。

发行验证脚本会检查 EXE/DLL 依赖闭包，并在移除开发 SDK PATH 后执行 CLI 解码、渲染和 MP3 导出冒烟。用户 SoundFont、原始背景文件、FFmpeg/ffprobe 命令行程序、Vulkan SDK 和 NVIDIA 驱动不会自动打包；视频帧解码库已部署，视频导出仍需要外部可执行文件。

`-FfmpegTestPath` 为测试 PATH 提供外部 FFmpeg；`-RequireVideoIntegration` 额外要求 Vulkan 构建、桌面及音频设备，直接执行全部视频背景集成模式，非零退出码（包括跳过码 77）均阻止打包。`build-info.json` 记录源码提交、是否有本地改动、工具版本、audioSmokePassed 和 videoIntegrationPassed；两个验证布尔值分别对应各自的可选门禁，为 false 表示未执行。后续源码变化不会自动刷新已有发行包。

## 16. 错误处理、取消和资源释放

### 16.1 错误传播

不同层使用不同错误边界：

- Reader：ReadResult.error；
- 领域和音频服务：QString* error 或 Error 状态；
- Application：errorOccurred、soundFontLoadFailed、settingsLoadWarning、settingsSaveFailed；
- 导出：带 Success、Failed、Canceled 的结果结构；
- CLI：stderr 和非零退出码。

跨线程错误不直接访问 UI 对象，而是通过 queued signal 返回 GUI 线程。

### 16.2 取消规则

文档加载使用 load generation 丢弃旧结果。音频和视频导出使用原子取消标志，在长循环、GPU 等待、FFmpeg 写入和进程等待点检查。取消不会把半成品提交为最终文件。

### 16.3 资源生命周期

- Qt 对象优先使用父子关系或明确的 unique_ptr；
- shared_ptr<const MusicDocument>、VisualChart 在读者之间共享，只读；
- PlaybackThread 停止前必须先停止 PlaybackSession；
- FluidSynth audio driver 停止并销毁后才能销毁 synth；
- Vulkan buffer、image、framebuffer、fence 和 mapped memory 按依赖顺序销毁；
- FFmpeg QProcess 关闭输入后等待完成，并在失败或取消时终止；
- 临时 SoundFont 或 PCM 文件在成功、失败和析构路径清理。

## 17. 测试体系

CMake 注册的测试按能力划分：

| 测试名 | 验证重点 |
| --- | --- |
| audio_export | 离线合成、尾音、重复段、节拍器、WAV/MP3 和取消 |
| soundfont_setup | 首次音源引导、验证、持久化和播放前错误 |
| note_color_modes | 配色模式、旧设置迁移、双后端传播 |
| theme_persistence_and_runtime | 主题默认值、持久化、运行时切换和高 DPI |
| note_visual_semantics_and_rendering | 音符语义、几何、动画、截图和性能诊断 |
| metronome_timeline_and_readers | 拍号、速度、弱起、重复段和节拍相位 |
| metronome_fluidsynth_audio | 真实 FluidSynth 节拍器混音、换音源和满载播放 |
| presentation_backend_configuration | Vulkan/传统后端边界和图形设置 |
| window_chrome_geometry/native/vulkan | 标题栏几何和平台窗口行为 |
| video_export_configuration | 视频参数、FFmpeg 探测和 Vulkan 门控 |
| video_encoder_roundtrip | FFmpeg 编码、音视频封装和 MP4 完整性 |
| video_background_timeline_and_decoder | 背景映射、结束定格、迁移、独立轨道时长、seek、持续拖动和过期结果隔离 |
| video_background_audio_export | 原声起点、精确导出时长和尾段静音；需要外部 FFmpeg |
| video_background_audio_device | Qt 真实音频后端、原声循环、短音轨静音区间和迟到事件；音量设为零 |
| video_background_vulkan | 离屏动态帧、三槽并发、像素对照及无背景跨槽等待 |
| video_background_full_export | 静音、仅原声、原声与乐曲混音的完整 MP4 导出 |
| video_background_window | Windows 真实 Vulkan 窗口、动态上传、循环及后端切换 |
| visualization_domain | 投影、可见窗口、区间索引和场景状态 |
| playback_session_transport | 播放、暂停、停止、seek、generation 和位置发布 |
| soundfont_inspector | SoundFont 容器检查 |

常规验证命令：

~~~powershell
ctest --test-dir build/windows-release -C Release --output-on-failure
~~~

需要外部 FluidSynth、SoundFont、FFmpeg、Vulkan 或音频设备的测试会按环境配置启用或跳过。视频背景项带 video-background 标签，集成项另带 integration 标签，缺少前置条件时返回 77。测试报告必须区分通过和跳过；严格视频门禁不接受跳过。原视频导出完整集成通过 `midi_play_video_export_tests.exe -platform offscreen --integration` 手动运行，不存在同名的 video_export_vulkan_integration CTest。完整参数和环境要求见 [视频背景验收](video-background.md#验证与发布)。自动测试不能替代目标机器上的实际听音、GPU 回退和发行包检查。

## 18. 关键不变量和扩展点

### 18.1 需要保持的不变量

1. MusicDocument 定稿后，PlaybackModel、VisualChart 和 PlaybackTimeline 只读共享。
2. 所有播放事件和视觉位置使用同一条展开后的 playback timeline。
3. 同一时间点的 MIDI 事件必须遵守 PlaybackEventMap 的协议优先级。
4. seek、stop、暂停恢复和音源切换必须 flush 旧音符并更新 generation。
5. UI 位置样本可以节流，音频事件调度不能依赖 UI 定时器。
6. Vulkan 后端不可用时，传统 Qt 后端仍必须能显示和播放。
7. 导出在验证完成前只能写临时文件，取消或失败不能破坏原目标文件。
8. 外部路径、动态库和 GPU 资源失效时必须通过可读错误返回。
9. 背景画面由音乐位置映射驱动；自然结束定格，显式停止与重播归零，原声不延伸到音乐终点之后。
10. 实时解码不因持续拖动而反复取消；只保留一个最新待处理目标，换源结果必须隔离；离屏动态背景资源仅在所属帧槽完成后复用。

### 18.2 可以扩展的边界

- 在 MusicReaderRegistry 注册新的格式 Adapter；
- 在 IPlaybackAudioService 后增加其他音频后端；
- 在 VisualChart 之上添加谱面、音游 lane 或交互判定视图；
- 增加独立 notation DOM 和排版引擎；
- 为 VideoExportService 增加新的编码器或容器；
- 为 SettingsService 增加版本化迁移；
- 在保持 PlaybackTimeline 语义的前提下加入更复杂的速度曲线。

扩展时应优先复用 MusicDocument、PlaybackTimeline、PlaybackModel 和 VisualChart 的稳定契约，避免让文件格式、Qt 控件或第三方库类型渗入领域核心。

## 19. 源码导航

| 主题 | 入口文件 |
| --- | --- |
| GUI 启动 | src/main.cpp |
| CLI 启动 | src/cli/main.cpp |
| 文件加载编排 | src/app/playerapplicationservice.cpp |
| 设置服务 | src/app/settingsservice.cpp |
| 音乐文档 | src/domain/music/musicdocument.h/.cpp |
| 重复展开与时间线 | src/domain/music/playbacktimeline.cpp、src/domain/music/musicdocument.cpp |
| 播放模型 | src/domain/playback/playbackmodel.cpp |
| 播放线程 | src/domain/playback/playbacksession.cpp |
| GUI 播放控制 | src/domain/playback/playbackcontroller.cpp |
| MIDI 解析 | src/infrastructure/midi |
| MusicXML 解析 | src/infrastructure/musicxml |
| Reader 注册 | src/infrastructure/readers |
| FluidSynth | src/infrastructure/audio |
| 可视化投影 | src/domain/visualization/playbackvisualizationprojector.cpp |
| 可见音符索引 | src/domain/visualization/visiblenoteindex.cpp |
| 背景视频时间映射 | src/domain/visualization/backgroundvideotimeline.h |
| 视频解码与轨道元数据 | src/infrastructure/media/videoframereader.h/.cpp |
| 实时视频背景与原声 | src/presentation/visualization/videobackgroundcontroller.h/.cpp |
| 导出视频原声处理 | src/infrastructure/encoding/backgroundaudioexport.h/.cpp |
| 传统绘制 | src/presentation/visualization/fallingnotesview.cpp、fallingnotesrenderer.cpp |
| Vulkan 实时绘制 | src/presentation/visualization/fallingnotesvulkanwindow.cpp |
| Vulkan 离屏导出 | src/presentation/visualization/offscreen/vulkanoffscreenrenderer.cpp |
| 音频导出 | src/app/audioexportservice.cpp、offlineaudiorenderer.cpp |
| 视频导出 | src/app/videoexportservice.cpp |
| FFmpeg 编码 | src/infrastructure/encoding |
| 持久化设置 | src/infrastructure/settings/qsettingsstore.cpp |
| 构建和测试注册 | CMakeLists.txt、CMakePresets.json |
