# MIDI Play

面向桌面端的 Qt Widgets 音乐播放器，读取 MusicXML 或标准 MIDI 文件，使用 FluidSynth 和 SoundFont 播放，并以音符下落视图呈现实时演奏状态。

> 当前发布基线：v0.2.7
> 当前已验证平台：Windows x64 / MSVC 2022
> 项目定位：播放与音游式可视化，不是 MuseScore 级别的严肃谱面排版编辑器。

## 目录

- [项目定位](#项目定位)
- [核心能力](#核心能力)
- [支持格式与边界](#支持格式与边界)
- [快速开始](#快速开始)
- [图形界面使用](#图形界面使用)
- [配置与音源](#配置与音源)
- [命令行验证](#命令行验证)
- [架构概览](#架构概览)
- [关键数据结构与算法](#关键数据结构与算法)
- [性能设计](#性能设计)
- [测试与验收](#测试与验收)
- [已知限制与后续方向](#已知限制与后续方向)
- [项目结构](#项目结构)
- [参与开发](#参与开发)
- [许可证](#许可证)

## 项目定位

MIDI Play 将“音乐文件导入、统一音乐语义、播放事件调度、SoundFont 音频输出和实时可视化”拆成相互独立的层次。它适合以下场景：

- 用 SoundFont 播放 MusicXML 或 MIDI 文件；
- 在不渲染五线谱的前提下查看音符、钢琴键和简谱下落动画；
- 验证音乐文件的节奏、音高、速度和控制器事件；
- 作为后续增加 MIDI 输入、音游交互或谱面视图的可扩展基础。

当前版本刻意不实现谱面编辑、五线谱排版、选择框、打印和 MuseScore 的完整 Engraving DOM。音乐文件首先被转换为播放域统一使用的 MusicDocument，再分别投影为播放事件和可视化模型。

## 核心能力

- **MusicXML 播放**：支持 .xml、.musicxml，并将 score-timewise 转换到统一的 score-partwise 语义入口。
- **MIDI 播放**：支持 .mid、.midi、.kar，包含 format 0/1/2、PPQN/SMPTE、Running Status、常用 Meta/SysEx、Program Change、Control Change、Pitch Bend、Channel Pressure、Poly Pressure 和踏板信息。
- **统一时间线**：使用 tick、微秒和预计算 tempo map 表达音乐时间，并支持 tick 与实际播放时间双向转换。
- **演奏语义**：支持反复段、ending、D.C.、D.S.、Segno、Coda、Fine 的基础播放展开，以及 tie、staccato、accent、tenuto、ghost、dynamic、hairpin 和 pedal 等播放相关语义。
- **实时播放控制**：播放、暂停、停止、拖动进度和 seek 后的音色/控制器/延音状态重建。
- **SoundFont**：默认使用 assets/midisound.sf2，支持从设置窗口加载 .sf2 或 .sf3，并支持播放中事务化切换。
- **下落式可视化**：显示音符、长音和踏板尾段、触发线、钢琴键、鼓轨、简谱、小节/节拍、歌词和标记。
- **可调视觉刷新率**：支持 30、60、120 FPS 及自定义整数刷新率；该设置只影响视觉位置发布和绘制，不改变音频调度精度。
- **平台标题栏选项**：原生标题栏为默认值；Windows 提供“自定义标题栏（实验）”，macOS/Linux 当前仅使用原生标题栏。
- **异步导入**：MusicXML/MIDI 解析和可视化投影在 QtConcurrent 工作线程中执行，避免阻塞界面线程。
- **可诊断性**：提供音频、MIDI reader、离屏渲染和普通文件解析 smoke test 入口。

## 支持格式与边界

| 输入 | 已支持 | 说明 |
| --- | --- | --- |
| MusicXML | .xml、.musicxml | 播放所需的 part、measure、note、rest、chord、grace、cue、backup、forward、tempo、direction、lyrics、marker、repeat 等语义 |
| 标准 MIDI | .mid、.midi | format 0/1/2；PPQN 和 SMPTE division；多轨道及通道归一化 |
| Karaoke MIDI | .kar | 按 MIDI 文件解析，并保留 lyric/meta 文本用于播放叠加 |
| 压缩 MusicXML | .mxl | 当前未注册 reader，不属于已支持输入 |
| MuseScore 工程 | .mscz、.mscx | 当前未支持 |

MIDI format 2 的独立序列会按源轨道顺序串联到统一播放时间线；format 0/1 的轨道保持并行。SMPTE division 使用固定帧率换算，tempo meta-event 不会覆盖固定 tick 速率。

MusicXML 和 MIDI 的导入结果都面向播放和音游式可视化。MusicAnalyzer 生成的调内 degree、量化网格、和弦组、tie 组、hold note、鼓组 lane 和调性置信度属于派生分析结果，不会替换原始播放时间。

## 快速开始

### Windows 前置条件

当前仓库已经验证的开发环境为：

- Windows x64；
- Visual Studio 2022，包含 Desktop C++ 工作负载；
- CMake >= 3.24；
- Qt >= 6.8 的 MSVC 64-bit 套件；
- 可提供 libfluidsynth-3.dll 的 FluidSynth 2.x/3.x 运行时；
- PowerShell（推荐使用 Developer PowerShell for VS 2022）。

项目不链接 Qt6::Multimedia。音频由 FluidSynth 的原生实时音频驱动输出，因此不会因为缺少 Qt Multimedia backend 而影响本项目的正常架构。

### 配置依赖路径

不要把本机 SDK 绝对路径写入项目文件。先在当前 PowerShell 会话中设置环境变量：

~~~powershell
$env:QT_ROOT = '<Qt MSVC 64-bit 套件根目录>'
$env:VCPKG_ROOT = '<vcpkg 根目录>'

if (-not (Test-Path "$env:QT_ROOT/bin/windeployqt.exe")) {
    throw 'QT_ROOT 未指向有效的 Qt MSVC 套件'
}
if (-not (Test-Path "$env:VCPKG_ROOT/vcpkg.exe")) {
    throw 'VCPKG_ROOT 未指向有效的 vcpkg 根目录'
}
~~~

VCPKG_ROOT 只用于准备 FluidSynth 运行时和辅助复制 DLL；当前程序通过 Qt QLibrary 在运行时解析 FluidSynth API，不需要在 CMake 中链接 FluidSynth import library。

### Debug 构建

~~~powershell
& "$env:VCPKG_ROOT/vcpkg.exe" install fluidsynth:x64-windows --classic
cmake --preset windows-msvc-debug -DCMAKE_PREFIX_PATH="$env:QT_ROOT" -DFLUIDSYNTH_DLL="$env:VCPKG_ROOT/installed/x64-windows/bin/libfluidsynth-3.dll"
cmake --build --preset windows-msvc-debug
& "$env:QT_ROOT/bin/windeployqt.exe" --debug --compiler-runtime 'build/windows-msvc-debug/Debug/midi_play.exe'
~~~

构建后，CMake 会在可用时自动复制：

~~~text
build/windows-msvc-debug/Debug/midi_play.exe
build/windows-msvc-debug/Debug/libfluidsynth-3.dll
build/windows-msvc-debug/Debug/assets/midisound.sf2
~~~

### Release 构建

可以复用同一个多配置 Visual Studio 构建目录：

~~~powershell
cmake --build build/windows-msvc-debug --config Release
& "$env:QT_ROOT/bin/windeployqt.exe" --release --compiler-runtime 'build/windows-msvc-debug/Release/midi_play.exe'
~~~

发行目录至少需要同时包含以下内容：

~~~text
midi_play.exe
libfluidsynth-3.dll
assets/midisound.sf2
platforms/qwindows.dll
由 windeployqt 根据实际依赖复制的 Qt 运行时 DLL
~~~

Qt DLL 和插件必须与目标架构、Qt 版本及构建类型一致。Debug 使用 qwindowsd.dll，Release 使用 qwindows.dll，两者不能混用。

如果 FluidSynth 不在 vcpkg 默认目录，可以显式指定：

~~~powershell
cmake --preset windows-msvc-debug -DCMAKE_PREFIX_PATH="$env:QT_ROOT" -DFLUIDSYNTH_DLL='<FluidSynth x64 DLL 的完整路径>'
~~~

## 图形界面使用

1. 启动 midi_play.exe。
2. 点击顶部“打开乐曲”，选择 MusicXML、MIDI 或 KAR 文件。
3. 点击底部播放、暂停或停止按钮。
4. 拖动底部进度条进行 seek。播放中释放后会直接从目标位置继续，暂停时释放后保持暂停。
5. 点击顶部“设置”打开独立设置窗口。

设置窗口提供：

- **视觉刷新率**：30 FPS、60 FPS、120 FPS 或“自定义”；
- **标题栏样式**：Windows 可选择原生或自定义实验模式，macOS/Linux 只显示原生模式；
- **音源**：选择 .sf2/.sf3，或恢复随程序提供的默认音源。

主页显示的是播放相关的音乐元数据和时间信息，不显示 SoundFont 文件名；音源路径及其状态在设置窗口中管理。

## 配置与音源

### 默认音源

默认 SoundFont 的相对路径为：

~~~text
assets/midisound.sf2
~~~

程序优先从可执行文件旁的 assets 目录查找，开发运行时再检查当前工作目录的 assets 目录。构建脚本会将仓库中的默认文件复制到输出目录。

### 用户级配置

配置使用 Qt QSettings 的 INI 格式，保存到：

~~~text
QStandardPaths::AppLocalDataLocation/settings.ini
~~~

具体物理目录由操作系统和 Qt 决定；Windows 通常位于当前用户的 Local AppData 下。当前配置键如下：

| 配置键 | 类型 | 默认值 | 说明 |
| --- | --- | --- | --- |
| General/schemaVersion | int | 3 | 设置结构版本 |
| General/visualizationRefreshRate | int | 60 | 有效范围 1..1000，界面提供常用预设和自定义输入 |
| General/titleBarMode | int | 0 | 0 为原生；Windows 上 1 为自定义实验模式 |
| Audio/soundFontPath | string | 空 | 空值表示使用随程序提供的默认音源 |

刷新率使用整数保存，便于高级用户直接编辑配置文件。无效值会回退到默认值，并通过设置加载警告提示。

自定义音源的行为：

- 用户主动选择或恢复默认音源后才更新持久化配置；
- 播放中切换音源会冻结播放位置、flush 当前音符、重新加载 SoundFont，并恢复通道状态和播放位置；
- 自定义文件被移动或删除时，本次启动会临时回退到默认音源，不会自动覆盖用户保存的自定义路径；
- 音频后端加载失败时，设置窗口显示实际错误，而不是永久保留“正在加载音源”状态。

## 命令行验证

以下命令均从项目根目录执行。$midiPlayExe 可以指向 Debug 或 Release 输出。

~~~powershell
$midiPlayExe = 'build/windows-msvc-debug/Debug/midi_play.exe'
~~~

### 直接解析文件

传入一个音乐文件路径时，程序执行 reader 和统一文档构建的 smoke test，然后退出：

~~~powershell
& $midiPlayExe 'path/to/example.musicxml'
& $midiPlayExe 'path/to/example.mid'
~~~

### 测试 FluidSynth 和 SoundFont

该命令加载指定 SF2，初始化 FluidSynth 原生音频驱动，并提交一组测试音符：

~~~powershell
& $midiPlayExe --audio-test 'assets/midisound.sf2'
~~~

运行前确认系统输出设备可用、系统音量未静音，且 libfluidsynth-3.dll 位于 exe 同级目录或系统 DLL 搜索路径中。

### 测试 MIDI reader

~~~powershell
& $midiPlayExe --midi-test 'path/to/example.mid'
~~~

命令会输出轨道数量和按 tempo map 换算得到的播放时长。

### 生成离屏可视化帧

~~~powershell
& $midiPlayExe --render-test 'path/to/example.musicxml' 'build/visualization.png' 10000000 1280 720
~~~

参数依次为：输入文件、输出 PNG、播放位置（微秒）、输出宽度和输出高度。播放位置、宽度和高度可以省略；默认播放位置为歌曲时长的十分之一，默认尺寸为 1280x720。

## 架构概览

~~~mermaid
flowchart LR
    A[MusicXML / MIDI / KAR] --> B[MusicReaderRegistry]
    B --> C[Format Adapter]
    C --> D[Parser / Normalizer / Builder]
    D --> E[MusicDocument]
    E --> F[MusicAnalyzer]
    F --> G[PlaybackModel]
    F --> H[VisualChart]
    G --> I[PlaybackSession]
    I --> J[EventScheduler / EventMap]
    J --> K[ThreadedPlaybackAudioService]
    K --> L[FluidSynthEngine]
    H --> M[VisibleNoteIndex / WindowCache]
    M --> N[FallingNotesView]
    N --> O[Qt Widgets / QPainter]
~~~

### 分层职责

| 层次 | 主要职责 | 关键对象 |
| --- | --- | --- |
| Presentation | 窗口、设置、进度条、可视化绘制和用户输入 | MainWindow、SettingsDialog、FallingNotesView |
| Application | 编排 reader、异步加载、播放会话和设置持久化 | PlayerApplicationService、SettingsService |
| Domain / Music | 与文件格式无关的音乐事实和时间语义 | MusicDocument、Track、NoteEvent、tempo map |
| Domain / Playback | 轨道事件、播放状态、seek、重复展开和状态恢复 | PlaybackModel、PlaybackSession、PlaybackController |
| Domain / Visualization | 从统一音乐模型生成不可变可视化模型和可见窗口 | VisualChart、VisibleNoteIndex、VisibleNoteWindowCache |
| Infrastructure / Readers | MusicXML、MIDI/KAR 的解析、归一化和构建 | MusicXmlReader、MidiFileParser、MidiNormalizer、MidiDocumentBuilder |
| Infrastructure / Audio | 动态解析 FluidSynth API，隔离音频后端类型 | FluidSynthEngine、FluidSynthAudioService、ThreadedPlaybackAudioService |
| Infrastructure / Settings | 使用 Qt QSettings 读写用户级 INI | QSettingsStore |

UI 不直接解析 XML/MIDI，也不直接调用 FluidSynth；播放域不依赖 Qt Widgets，音频后端类型不会泄漏到音乐领域模型。后续可以注册新的 reader、替换音频后端或增加独立谱面视图，而不改变已有输入和播放契约。

## 关键数据结构与算法

- **MusicDocument**：统一保存轨道、音符、小节、tempo、调号、拍号、歌词和标记，并提供 tick/微秒转换。
- **Track / NoteEvent**：保存轨道级通道、program、鼓组信息，以及音高、力度、时值、voice、staff、articulation 和稳定 noteId。
- **PlaybackSegment**：将源小节区间映射到反复展开后的输出时间线，保留 repeat pass 和源小节索引。
- **PlaybackModel**：把 MusicDocument 变成每轨道播放事件，同时维护全局事件索引和 PlaybackStateSnapshot，用于 seek 后恢复 Program Change、控制器、Pitch Bend 和跨目标位置的长音。
- **PlaybackSession**：维护播放状态、权威 playhead、事件 generation 和调度窗口。播放线程使用高精度短周期检查事件，UI 只接收按设置节流后的位置样本。
- **VisualChart**：不可变的可视化投影，包含投影后的开始时间、键释放时间、可听结束时间、轨道颜色、简谱 degree、鼓组 lane、和弦实例和稳定实例 ID。
- **VisibleNoteIndex**：带 subtree max-end 的平衡区间索引，按时间窗口查询可见音符，避免每帧扫描完整曲目。
- **VisibleNoteWindowCache**：当播放窗口仍在上一次查询范围内时复用候选音符，只有窗口越界或 seek 时才重新查询。
- **ActiveNoteLookup**：使用固定大小 lookup 维护当前激活音符和鼓组 lane，避免在每帧通过线性容器反复查找。

音频与视觉使用同一份音乐时间语义，但职责不同：FluidSynth 原生音频驱动负责音频帧输出，播放会话使用软件单调时钟提交 MIDI 事件，视觉层读取权威 transport 位置并按刷新率绘制。Qt UI 定时器不负责累加音频播放时间。

## 性能设计

当前渲染路径针对高密度 MIDI 做了以下优化：

- 可见音符按时间区间索引查询，复杂度约为 O(log N + K)，其中 K 是窗口内候选数量；
- 可见窗口在小步移动时复用查询结果，避免每个位置样本都清空并重建候选数组；
- 每帧只构建一次激活音符状态，音高查找使用固定大小 lookup；
- 钢琴键、八度标签和鼓组键盘进入静态图像缓存；背景填充和黑键分隔带使用预计算几何与批量绘制，播放中只保留必要的动态绘制；
- 音符画笔、颜色、文本布局和部分几何结果按静态音符属性缓存，减少高密度场景中的临时对象和状态切换；
- 绘制路径关闭不必要的全局抗锯齿，并使用适合矩形、网格和钢琴键的栅格策略；
- 播放线程的高频事件检查与 UI 位置发布解耦，视觉刷新率变化不会改变音符事件的时间精度。

这些优化改善的是 CPU 使用率和界面响应，不代表在任意 4K、高 DPI、超高密度曲目上都能保证屏幕级 v-sync。若后续基准测试表明 QWidget + QPainter 仍无法满足目标，再评估 Qt Quick scene graph/RHI。

## 测试与验收

### 自动测试

配置并构建后运行：

~~~powershell
ctest --test-dir build/windows-msvc-debug -C Debug --output-on-failure
~~~

当前测试目标包括：

- visualization_domain：可视化投影、时间窗口、区间索引和场景数据；
- playback_session_transport：播放、暂停、停止、seek、事件代际和 transport 状态。

普通 PowerShell 如果找不到 ctest，请调用与 CMake 同目录的 ctest.exe，或使用 Visual Studio Developer PowerShell。

### 人工验收建议

| 场景 | 验收点 |
| --- | --- |
| MusicXML | 能打开、显示下落音符、读取 tempo/拍号/调号，并正常播放 |
| MIDI format 0/1 | 多轨道同时播放，program/channel 和 tempo 基本正确 |
| MIDI format 2 | 独立序列按规范串联，播放时长和轨道顺序合理 |
| 拖动进度 | 播放中释放后继续播放，暂停中释放后保持暂停，下一次播放从目标位置开始 |
| SoundFont | 默认 SF2 可加载，自定义 SF2/SF3 可切换，失败时显示错误并保留可恢复状态 |
| 设置持久化 | 重启后刷新率、标题栏模式和自定义音源路径仍可恢复 |
| Release 部署 | exe、Qt 平台插件、FluidSynth DLL 和 assets/midisound.sf2 均可找到 |

自动测试不替代人工听音验收；音频设备、系统音量和 FluidSynth 驱动初始化仍需在目标机器上确认。

## 已知限制与后续方向

- 当前只验证 Windows x64 / MSVC；macOS/Linux 的 Qt 架构分支已预留，但没有同等完整的构建、部署和音频验收基线。
- 当前只有 FluidSynth 音频后端，不提供 Qt Multimedia 后端、外部 MIDI 硬件输出或音频文件导出。
- MusicXML 解析面向播放所需语义，不等价于完整的 MuseScore notation DOM；复杂排版、符号布局和编辑语义不在当前范围内。
- 当前不支持 .mxl、.mscx、.mscz 等压缩或 MuseScore 专用工程格式。
- 简谱、鼓组 lane、量化网格和调性识别是播放可视化的派生数据，不能当作完整的自动扒谱结果。
- Windows 自定义标题栏是实验功能，默认仍使用原生标题栏；macOS/Linux 不提供自定义标题栏选项。
- 播放事件使用软件单调时钟调度，视觉位置按 transport 样本更新；当前目标是稳定播放和低 CPU 占用，不承诺严格的显示器 v-sync 或音频帧级视觉同步。

后续可沿以下方向扩展：

1. 增加 .mxl 和更多 MIDI/导入边界的兼容测试；
2. 增加可插拔音频后端和外部 MIDI 输出；
3. 将音游 lane、判定和交互输入建立在现有 VisualChart 之上；
4. 建立独立的 notation DOM 和布局引擎，而不污染播放域模型；
5. 根据跨平台基准测试评估 Qt Quick scene graph/RHI。

## 项目结构

~~~text
assets/
  midisound.sf2                       默认 SoundFont
src/
  app/                                应用服务和设置服务
  domain/music/                       音乐文档、时间线和分析
  domain/playback/                    播放模型、会话、调度和状态
  domain/visualization/               可视化投影、索引和场景状态
  domain/settings/                    设置值对象和平台策略
  infrastructure/musicxml/            MusicXML reader
  infrastructure/midi/                MIDI parser、normalizer、builder
  infrastructure/readers/             通用 reader registry 和 adapter
  infrastructure/audio/               FluidSynth 动态适配和线程边界
  infrastructure/settings/            QSettings INI 存储
  infrastructure/resources/           默认资源定位和项目资源模型
  presentation/                       Qt Widgets、设置窗口和渲染器
tests/                                领域和 transport 测试
CMakeLists.txt                        构建目标、资源复制和安装规则
CMakePresets.json                     Windows MSVC / Ninja 预设
vcpkg.json                            FluidSynth 逻辑依赖声明
~~~

## 参与开发

提交代码前建议完成：

1. 使用与目标平台匹配的 Qt、编译器和 FluidSynth 运行时；
2. Debug 或 Release 构建通过；
3. ctest 全部通过；
4. 对涉及播放时序、seek、SoundFont 或渲染性能的改动补充对应测试或验收说明；
5. 保持领域层不依赖 Qt Widgets 和 FluidSynth 具体类型。

问题反馈请尽量附带：操作系统、构建类型、输入文件格式、SoundFont 类型、复现步骤和相关日志。音频问题还应说明系统默认输出设备和 libfluidsynth-3.dll 的实际位置。

## 许可证

当前仓库未包含 LICENSE 文件，因此尚未声明可复用许可证。分发、二次开发或集成前，请先确认项目维护者提供的授权范围；后续应在仓库根目录补充正式许可证文件。
