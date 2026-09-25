# MIDI Play

<p align="center">
  <img src="docs/images/midiplay-banner.svg" alt="MIDI Play 横版 Logo" width="560">
</p>

播放 MIDI 与 MusicXML，让音乐以彩色下落音符呈现。

MIDI Play 是一款开源桌面音乐播放器，提供实时钢琴键盘、深浅色主题、鲜明音符配色、播放变速与节拍器。界面基于 Qt Widgets，音频由 FluidSynth 和用户选择的 SoundFont 驱动，默认优先使用 Vulkan 绘制，同时保留传统 Qt 绘制作为兼容和故障回退模式。

[下载 Windows x64 发行版](https://github.com/allenwu258/midi-play/releases/latest) · [快速开始](#快速开始) · [从源码构建](#源码构建) · [反馈问题](https://github.com/allenwu258/midi-play/issues)

当前发布版本：[v0.4.0](https://github.com/allenwu258/midi-play/releases/tag/v0.4.0)，首次提供完整 Windows x64 便携发行包。此前版本通过 Git Tag 记录开发进展。

> 播放前需自行准备本地 SF2 / SF3 音源。程序不附带乐曲音源，也不会自动下载；可以跳过首次配置，先打开并查看乐曲。

## 目录

- [项目定位](#项目定位)
- [核心能力](#核心能力)
- [支持格式与边界](#支持格式与边界)
- [快速开始](#快速开始)
- [源码构建](#源码构建)
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
- **实时播放控制**：播放、暂停、停止、拖动进度、20%～200% 播放变速，以及 seek 后的音色/控制器/延音状态重建。
- **节拍器**：独立点击音源与合成器，和歌曲分开管理复音、通过同一音频设备混音输出；跟随乐曲拍号、速度变化和重复段落，并随 20%～200% 播放倍率同步变速。
- **SoundFont**：不内置乐曲音源，启动时检查用户已配置的 SF2/SF3；未配置或加载失败时引导选择，允许暂时跳过。支持播放中事务化切换，SF3 由启用 libsndfile/Ogg Vorbis 的 FluidSynth 后端解码。
- **下落式可视化**：显示音符、长音和踏板尾段、触发线、钢琴键、鼓轨、简谱、小节/节拍、歌词和标记。
- **双渲染模式**：默认优先使用 Vulkan 绘制，传统 Qt 绘制作为兼容和故障回退模式；两者共享音符布局、主题、色彩和单图片背景设置，也可在设置中手动选择。
- **单图片背景**：可为下落音符区域选择本地 PNG、JPEG、静态 WebP 或 BMP 图片；WebP 由随程序部署的解码库读取，不依赖 Qt WebP 插件。图片异步加载、按区域比例裁剪并带可读性遮罩，Vulkan 与传统 Qt 绘制共享配置。
- **可调视觉刷新率**：支持 30、60、120 FPS 及自定义整数刷新率；该设置只影响视觉位置发布和绘制，不改变音频调度精度。
- **平台标题栏选项**：原生标题栏为默认值；Windows 提供“自定义标题栏（实验）”，macOS/Linux 当前仅使用原生标题栏。
- **深色 / 浅色主题**：设置中切换并自动保存；两套主题同时覆盖控件、下落音符和琴键，兼容传统 Qt 与 Vulkan 绘制。
- **普通 / 鲜明音符色彩**：默认鲜明，使用协调色系呈现同轨音符的色彩层次；普通保留原有柔和配色。设置立即生效并持久化，两种渲染模式共享材质。
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

1. 前往 [Releases](https://github.com/allenwu258/midi-play/releases/latest)，下载附件 `midi-play-v0.4.0-windows-x64.zip`。GitHub 自动提供的 `Source code` 是源码包。
2. 完整解压 ZIP，运行其中的 `midi_play.exe`，保留同目录 DLL、插件和许可证文件。
3. 在启动引导中选择本地 `.sf2` 或 `.sf3` 音源。多轨 MIDI 建议使用覆盖完整 General MIDI（GM）乐器的音源；选择成功后会尝试自动保存路径。
4. 点击“打开乐曲”，选择 `.mid`、`.midi`、`.kar`、`.xml` 或 `.musicxml` 文件，再点击播放。
5. 在“设置”中调整主题、音符色彩、简谱条显隐及图形模式。

发行包无需安装，也不需要安装 Qt、Visual Studio 或 Vulkan SDK。当前已验证平台为 **Windows x64**；新用户首次启动会优先尝试 Vulkan，首次渲染成功后保存该选择，初始化失败则自动回退并保存传统 Qt 绘制。Vulkan 模式仍需要兼容的显卡及驱动，遇到显示问题时也可在设置中切回传统 Qt 绘制。

首次配置可以选择“暂时跳过”。没有有效音源时仍可导入、查看乐曲和调整进度，点击播放只显示行内提示；随后从设置中加载音源即可播放。详细行为见 [配置与音源](#配置与音源)，当前版本的问题见 [已知限制与后续方向](#已知限制与后续方向)。

压缩 MusicXML（`.mxl`）和 MuseScore 工程（`.mscz` / `.mscx`）尚不支持，请先从制谱软件导出为普通 MusicXML 或 MIDI。

## 源码构建

以下步骤适用于希望自行编译或参与开发的用户。直接使用播放器可下载上面的便携发行包。

### Windows 前置条件

一键发行构建支持 Windows x64，使用普通的 **64 位 PowerShell 5.1 或 PowerShell 7**，不要求预先打开 Developer PowerShell。请安装：

- **Visual Studio 2022 / Build Tools 2022**：包括“使用 C++ 的桌面开发”、MSVC v143 x64 工具、Windows SDK 和 C++ CMake 工具；
- **Qt 6.8 或更新的 Qt 6 MSVC x64 套件**：包含 Core、Gui、Widgets、Concurrent、Xml 和 `windeployqt`；MinGW、ARM64 和静态 Qt 套件不适用；
- **Git**：`git.exe` 可从 PATH 找到；
- **vcpkg**：准备独立的 vcpkg checkout，例如使用 `git clone https://github.com/microsoft/vcpkg.git <目标目录>`；脚本会在缺少 `vcpkg.exe` 时执行该 checkout 的 bootstrap；
- **Vulkan SDK**：构建 Vulkan 版本时需要 x64 头文件、导入库和 `glslangValidator`。只构建传统版本可以不安装；
- **CMake >= 3.24**：默认使用所配置 Visual Studio 附带的版本，也可以单独指定。

不需要预先安装 FluidSynth 或 libwebp。脚本根据 `vcpkg.json` 自动下载或恢复缓存并构建 FluidSynth、libsndfile、libwebp 和 Ogg/Vorbis/FLAC/Opus 等传递依赖。首次构建需要联网，耗时取决于网络和 vcpkg 缓存；后续构建复用已安装依赖。程序通过 `QLibrary` 使用 FluidSynth，不链接 Qt Multimedia。

### 统一环境配置

在仓库根目录执行：

~~~powershell
Copy-Item build.env.sample.psd1 build.env.psd1
notepad build.env.psd1
~~~

`build.env.psd1` 是 PowerShell **数据文件**，通过 `Import-PowerShellDataFile` 读取，不作为脚本执行。路径写在单引号中，反斜杠不用转义；不支持 `$env:...` 或命令插值。填写自己安装的 SDK 路径即可：

| 配置项 | 含义 |
| --- | --- |
| `VisualStudioRoot` | VS 2022 或 Build Tools 实例根目录，包含 `Common7` 和 `VC` |
| `QtRoot` | Qt 的 MSVC x64 套件目录，包含 `bin`、`lib`、`plugins`，不是 Qt 安装器根目录 |
| `VcpkgRoot` | vcpkg checkout 根目录，包含 `scripts/buildsystems/vcpkg.cmake` |
| `VulkanSdk` | Vulkan SDK 版本目录，包含 `Include`、`Lib`、`Bin`；传统构建可留空 |
| `CMakeExe` | 可选的 `cmake.exe` 绝对路径，空字符串表示使用上述 VS 实例的 CMake |
| `EnableVulkan` | `$true` 构建 Vulkan 版本，`$false` 仅构建传统版本 |

Git 只保存 `build.env.sample.psd1`。实际配置 `build.env.psd1` 和 `build.env.*.local.psd1` 已加入 `.gitignore`，不会被复制到发行包。构建不读取 `documents/SDK` 或任何开发机私有说明文件，也不向系统写入永久环境变量。

仅检查环境：

~~~powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/Build-Windows.ps1 -CheckEnvironment
~~~

配置文件缺失、路径错误、工具缺失或架构不匹配时，脚本会报错并说明配置位置，不继续生成发行目录。`-ExecutionPolicy Bypass` 只作用于本次 PowerShell 进程。

### 一键发行构建

~~~powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/Build-Windows.ps1
~~~

脚本按以下顺序执行：

1. 检查配置并初始化指定 VS 实例的 x64 编译环境，显式选用配置的 Qt 和 Vulkan，清除调用者残留的 SDK 搜索变量；
2. 使用清单模式安装 `x64-windows` 依赖到 `build/dependencies/vcpkg_installed`，不修改 vcpkg 的 classic installed 目录。`vcpkg.json` 中的 `builtin-baseline` 固定依赖版本解析基线；
3. 重新配置 CMake，复用对象文件进行 Release 构建，并运行全部 CTest；启用 Vulkan 时缺少任何必需能力会直接失败，不会静默产出传统版本；
4. 在独立临时目录安装 GUI、CLI（不复制用户音源），用 `windeployqt` 部署 Qt DLL/插件，部署 FluidSynth 的传递依赖及 VS 提供的 **app-local MSVC CRT DLL**；
5. 移除 PATH 中的开发 SDK，检查每个 EXE/DLL/插件的依赖闭包，并运行发行包 CLI 和实际 Qt 渲染检查；
6. 校验成功后替换正式发行目录，写入不含本机 SDK 路径的 `build-info.json`，附带项目许可证、vcpkg 依赖版权文件及 SDK 提供的 Qt SBOM。

默认结果如下，整个文件夹即可用于分发，不生成 ZIP：

~~~text
dist/midi-play-windows-x64/
  midi_play.exe
  midi_play_cli.exe
  Qt6*.dll
  libfluidsynth-3.dll
  sndfile.dll、ogg.dll、vorbis*.dll 等实际依赖
  msvcp140*.dll、vcruntime140*.dll 等 MSVC CRT
  platforms/qwindows.dll
  styles/、imageformats/ 等 Qt 插件
  licenses/
  LICENSE
  build-info.json
~~~

`build/windows-release/Release` 是开发构建目录，**请分发 `dist` 中的完整目录**。用户机器不需要安装 Qt、vcpkg、MSVC 或 Vulkan SDK；Windows 系统组件和支持 Vulkan 的显卡驱动仍由操作系统/驱动提供，发行包不会携带 SDK 的 Vulkan loader 或验证层。

重复运行会复用编译结果，但每次重新部署一个干净的临时目录，防止旧 DLL 混入新包。构建或验证失败不会替换上一份成功的发行目录；部署阶段失败时会保留路径中带 `staging` 的目录用于诊断。同一仓库的脚本构建使用排他锁。不要向脚本生成的正式发行目录中存放个人文件，它会在下次成功构建时整体替换。

如果新包已经发布，但旧目录中的文件被正在运行的程序占用，脚本会提示旧目录保留的位置；新包仍可使用。

### 常用选项

~~~powershell
# 不要求 Vulkan SDK，另存一份传统版本
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/Build-Windows.ps1 -Traditional -BuildName windows-traditional -PackageName midi-play-traditional

# 同时验证发行包的真实 FluidSynth 音频输出，会播放约 1.5 秒测试音
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/Build-Windows.ps1 -AudioSmoke -SoundFontPath 'C:\SoundFonts\example.sf3'

# 使用另一份本地配置并限制并行度
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/Build-Windows.ps1 -EnvironmentFile build.env.other.local.psd1 -Jobs 4
~~~

`-AudioSmoke` 需要可用音频设备和显式指定的 `-SoundFontPath`，失败也会阻止发行目录替换；默认构建不依赖音频设备。外部测试音源不会进入发行包；指定 `-SoundFontPath` 还会启用启动配置及实际播放集成测试。直接使用 CMake 时可设置 `MIDI_PLAY_TEST_SOUNDFONT`。`-CheckEnvironment` 只检查环境，不下载依赖或编译。`-BuildName` 和 `-PackageName` 只接受字母、数字、下划线和连字符，输出固定限制在本仓库的 `build` 和 `dist` 下。

常见错误的处理：

| 情况 | 处理 |
| --- | --- |
| 未找到环境文件 | 复制 sample，填写路径后重试 |
| 找不到 `cl.exe`、Windows SDK 或 CRT | 在配置的 VS 实例中补装 C++ 工作负载和 v143 工具 |
| Qt 为 MinGW/ARM64 或版本过低 | 修改 `QtRoot`，选择 Qt 6.8+ MSVC x64 套件 |
| Vulkan 检查失败 | 补装 SDK / 使用支持 Vulkan 的 Qt，或使用 `-Traditional` |
| vcpkg 下载失败 | 检查访问源码站点的网络/代理配置后重试；脚本保留原始诊断输出 |
| 正式发行目录正在使用 | 关闭该目录启动的程序后重试 |
| 依赖闭包不完整 | 根据报错检查 Qt/VS/vcpkg 安装；不要从其他版本手工混入 DLL |

### 手动开发构建

一键脚本只生成经过测试和部署的 Release 包。需要 Debug、IDE 或跨平台开发时仍可直接使用 CMake；预设中的 Qt 路径来自 `QT_ROOT`，不包含本机固定路径。以下示例在准备好 CMake 和 MSVC 的 Developer PowerShell 中执行；Qt 和依赖路径来自同一个数据文件及上述脚本创建的依赖目录：

~~~powershell
$buildEnvironment = Import-PowerShellDataFile ./build.env.psd1
$env:QT_ROOT = $buildEnvironment.QtRoot
$env:VULKAN_SDK = $buildEnvironment.VulkanSdk
cmake --preset windows-msvc-debug -DFLUIDSYNTH_DLL="$PWD/build/dependencies/vcpkg_installed/x64-windows/bin/libfluidsynth-3.dll" -DWebP_DIR="$PWD/build/dependencies/vcpkg_installed/x64-windows/share/WebP"
cmake --build --preset windows-msvc-debug
~~~

直接使用 CMake 时，`MIDI_PLAY_ENABLE_VULKAN=ON` 默认允许在依赖缺失时降级；`MIDI_PLAY_REQUIRE_VULKAN=ON` 可要求必须成功启用。明确指定 `FLUIDSYNTH_DLL` 的优先级最高。底层 CMake 安装规则保留开发用途，一键发行脚本会额外完成 Qt/CRT 部署及闭包校验。Debug 插件和 Release 插件不能混用，Debug 输出不作为发行包。

## 图形界面使用

1. 启动 `midi_play.exe`。
2. 若提示配置音源，选择本地 SF2/SF3；也可暂时跳过，稍后从设置中加载。
3. 点击顶部“打开乐曲”，选择 MusicXML、MIDI 或 KAR 文件。
4. 音源可用后，点击底部播放、暂停或停止按钮。
5. 拖动底部进度条进行 seek。播放中释放后会直接从目标位置继续，暂停时释放后保持暂停。
6. 悬停底部播放键右侧的百分比按钮，拖动滑块调节播放速度，范围 **20%～200%**，步进 1%。也可点击按钮，在滑块右侧输入百分比，按回车或移开焦点生效；Esc 取消尚未确认的输入并关闭面板。
7. 点击播放键右侧的“节拍器”按钮开关节拍器。开启后按曲目的拍号、速度和反复展开实时点击，暂停、seek、停止和调速均保持同步。
8. 点击顶部“设置”打开独立设置窗口。

播放速度默认 **100%**。播放中变速不重启音频会话、不改变音高，音符、踏板、控制器事件和下落画面共用调整后的播放时钟。暂停、停止、拖动进度、换曲和切换音源保留本次选择的倍率，重启程序恢复 100%。进度、总时长和顶部 BPM 均按原曲音乐时间显示；例如 200% 播放时，原曲进度每秒推进约两秒。

节拍器默认关闭，开启后按钮高亮；仅在播放时发声，小节首拍使用较强、较高的点击音。播放中开启会从下一拍加入，暂停、停止、拖动进度和切换音源会取消旧点击音，卡顿后跳过错过的节拍，不连续补响。开关在本次运行中保留，重启后恢复关闭。使用程序内嵌的原创短点击音源，不依赖所选 SF2/SF3 的打击乐音色，也不占用歌曲的 16 个 MIDI 通道。

节拍规则优先采用文件中的明确标记：MIDI 的 `FF 58` 点击间隔和每 MIDI 四分音符对应的记谱单位、MusicXML 的节拍单位及附点；无明确标记时，简单拍按分母单位点击，6/8 等复合拍按三个分母单位分组，`3+2/8` 等加法拍按指定分组。MusicXML 使用实际小节边界，首个标为 `implicit="yes"` 的不完整小节按弱起处理；MIDI 缺少弱起信息时从文件起点建立小节。节拍器与传统/Vulkan 背景线共同使用领域层 `MusicRhythmGrid` 的小节边界和弱起相位，视觉保留按分母单位生成的细分拍线；重复速度标记不会重新起拍。SMPTE 时间码、无效拍号或超过一百万个网格点的异常乐曲不生成音乐网格并禁用节拍器，悬停按钮可查看原因，普通音乐播放仍可使用。

设置窗口提供：

- **界面主题**：深色（默认）或浅色。选择立即生效并自动保存，重启后恢复；切换不改变播放位置、速度或音源；
- **音符色彩**：鲜明（默认）或普通，与界面主题独立。鲜明按轨道色系、音高及打击乐类别提供稳定的协调配色，普通保留 v0.3.3 的视觉效果；修改立即生效并自动保存；
- **视觉刷新率**：30 FPS、60 FPS、120 FPS 或“自定义”；
- **图形模式**：Vulkan（推荐）或传统 Qt 绘制（兼容）；新用户首次启动优先尝试 Vulkan，成功后保存 Vulkan，失败后自动保存传统 Qt；未包含 Vulkan 的构建只提供传统模式；
- **下落背景**：选择“无背景”或“图片背景（实验）”；选择图片背景后显示图片选择项。图片只覆盖下落音符区域，不覆盖钢琴键和播放控制栏；无法读取的图片不会被应用。
- **显示简谱条**：默认关闭。关闭时移除琴键上方的简谱条及黄色判定线，音符在琴键顶部判定；开启后恢复简谱条和黄线。修改立即生效并自动保存，两种图形模式行为一致；
- **标题栏样式**：Windows 可选择原生或自定义实验模式，macOS/Linux 只显示原生模式；
- **音源**：选择或更换本地 .sf2/.sf3，成功加载后自动保存选择；加载失败会显示内联错误。

主页显示的是播放相关的音乐元数据和时间信息，不显示 SoundFont 文件名；音源路径及其状态在设置窗口中管理。

## 配置与音源

### 首次配置音源

程序不附带乐曲 SoundFont，也不会自动下载或回退到内置音源。每次启动会在后台检查已保存的音源路径，并通过 FluidSynth 验证其可加载性。没有配置、文件已移走、格式损坏或后端无法解码时，会显示“配置播放音源”对话框，提供“选择音源…”和“暂时跳过”。选择框支持 `.sf2` 和 `.sf3`，取消选择不会覆盖已有配置。

跳过后可以正常导入、查看乐曲及调整进度；没有有效音源时点击播放，会在主界面显示与设置页相同风格的内联错误，不弹窗、不推进播放。之后从设置中成功加载音源即可播放，无需重新导入乐曲。正在检查音源时需等待加载结束；选择成功后才持久化路径。

路径保存在用户级设置中，音源文件仍由用户自行管理，不会复制进程序目录。建议将文件放在稳定的本地位置，并选用覆盖所需乐器的音源（多轨 MIDI 通常需要完整 GM 音源）。关闭或跳过配置只影响本次启动，下次启动仍会检查。命令行指定的乐曲会在启动检查或跳过后导入。

### 用户级配置

配置使用 Qt QSettings 的 INI 格式，保存到：

~~~text
QStandardPaths::AppLocalDataLocation/settings.ini
~~~

具体物理目录由操作系统和 Qt 决定；Windows 通常位于当前用户的 Local AppData 下。当前配置键如下：

| 配置键 | 类型 | 默认值 | 说明 |
| --- | --- | --- | --- |
| General/schemaVersion | int | 8 | 设置结构版本 |
| General/themeMode | int | 0 | 0 为深色；1 为浅色；旧配置缺失或无效时回退深色 |
| General/noteColorMode | int | 1 | 0 为普通；1 为鲜明；缺失或无效时回退鲜明，不重置其他偏好 |
| General/visualizationRefreshRate | int | 60 | 有效范围 1..1000，界面提供常用预设和自定义输入 |
| General/graphicsMode | int | 未配置时优先 Vulkan | 持久化值 0 为传统 Qt 绘制；1 为 Vulkan；首次解析成功后保存实际使用的后端 |
| Visualization/backgroundImagePath | string | 空 | 单图片背景的绝对路径；空值表示未选择图片，文件只在本地引用，不复制到程序目录 |
| Visualization/backgroundImageEnabled | bool | false | 是否启用图片背景；关闭时保留已选路径，旧配置有图片路径且缺少本项时默认启用 |
| General/showNotationStrip | bool | false | 是否显示简谱条；缺少此配置键的旧版配置也默认隐藏 |
| General/titleBarMode | int | 0 | 0 为原生；Windows 上 1 为自定义实验模式 |
| Audio/soundFontPath | string | 空 | 用户音源的绝对路径；空值表示尚未配置 |

刷新率使用整数保存，便于高级用户直接编辑配置文件。无效值会回退到默认值，并通过设置加载警告提示。

升级后，旧配置缺少音符色彩项时默认使用鲜明；希望沿用原有外观可在设置中选择普通。读取其他设置的默认值不会主动写入配置；图形模式是例外，新用户首次完成渲染后会保存实际成功的后端，已有 `General/graphicsMode` 配置则保持原选择。背景图片路径只在用户选择后保存；切换到“无背景”时保留路径供重新启用，启动时仅对已启用的图片异步读取，文件缺失或损坏时保留配置但回退主题背景。保存失败会在设置窗口提示，当前会话保留所选外观，重启时仍以实际保存的配置为准。

自定义音源的行为：

- 仅成功加载的音源选择会写入持久化配置；
- 播放中切换音源会冻结播放位置、flush 当前音符、重新加载 SoundFont，并恢复通道状态和播放位置；
- 音源被移动或删除时会提示重新选择，不会自动覆盖用户保存的路径；旧版本的自定义路径继续使用，未配置自定义音源的用户升级后需要选择音源；
- 音频后端加载失败时，设置窗口显示实际错误，而不是永久保留“正在加载音源”状态。

## 命令行验证

以下命令均从项目根目录执行。`midi_play.exe` 是主播放器，采用 Windows GUI subsystem，双击启动时不显示命令行窗口。`midi_play_cli.exe` 是 Console 程序，负责所有自动化、诊断和离屏渲染命令。

~~~powershell
$midiPlayCliExe = 'dist/midi-play-windows-x64/midi_play_cli.exe'
~~~

### 直接解析文件

传入一个音乐文件路径时，程序执行 reader 和统一文档构建的 smoke test，然后退出：

~~~powershell
& $midiPlayCliExe 'path/to/example.musicxml'
& $midiPlayCliExe 'path/to/example.mid'
~~~

### 测试 FluidSynth 和 SoundFont

该命令加载指定 SF2 或 SF3，初始化 FluidSynth 原生音频驱动，并提交一组测试音符：

~~~powershell
& $midiPlayCliExe --audio-test 'path/to/example.sf2'
& $midiPlayCliExe --audio-test 'path/to/example.sf3'
& $midiPlayCliExe --audio-test 'path/to/example.sf2' 'path/to/example.sf3'
~~~

第三条命令额外验证播放中从 SF2 切换到 SF3。运行前确认系统输出设备可用、系统音量未静音，且 FluidSynth 及其 SF3 codec DLL 位于 exe 同级目录或系统 DLL 搜索路径中。若 SF3 加载失败，程序会区分后端未启用 SF3、缺少 codec 依赖和文件内容损坏。

### 测试 MIDI reader

~~~powershell
& $midiPlayCliExe --midi-test 'path/to/example.mid'
~~~

命令会输出轨道数量和按 tempo map 换算得到的播放时长。

### 生成离屏可视化帧

~~~powershell
& $midiPlayCliExe --render-test 'path/to/example.musicxml' 'build/visualization.png' 10000000 1280 720
~~~

参数依次为：输入文件、输出 PNG、播放位置（微秒）、输出宽度和输出高度。播放位置、宽度和高度可以省略；默认播放位置为歌曲时长的十分之一，默认尺寸为 1280x720。

可在末尾添加 `--theme dark` 或 `--theme light` 指定截图主题；省略时固定使用深色，不读取桌面应用的用户设置，便于重复比较。主题架构和验收范围见 [主题开发方案](docs/theme-development-plan.md)。

音符色彩使用 `--note-colors normal` 或 `--note-colors vivid`，省略时固定为鲜明，可与主题参数组合。对比 v0.3.3 时须显式传入 `normal`；参数缺值、非法或重复时返回错误码 2，截图命令不改写用户偏好。

```powershell
& $midiPlayCliExe --render-test 'path/to/example.mid' 'build/vivid.png' 10000000 1280 720 --theme light --note-colors vivid
```

共享材质、音高配色和缓存设计见 [音符色彩模式方案及实施记录](docs/note-color-modes-development-plan.md)。

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
    N --> P[QVulkanWindow / Vulkan]
~~~

### 分层职责

| 层次 | 主要职责 | 关键对象 |
| --- | --- | --- |
| Presentation | 窗口、启动音源引导、设置、进度条、双后端绘制和用户输入 | MainWindow、SoundFontSetup、SettingsDialog、FallingNotesView、FallingNotesVulkanWindow |
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

传统模式通过上述缓存减少 CPU 绘制开销；Vulkan 模式使用 GPU 绘制，并复用共享的布局、音符材质与播放状态。实际性能取决于分辨率、曲目密度、硬件和驱动，不保证所有场景下都达到显示器刷新率。

## 测试与验收

### 自动测试

配置并构建后运行：

~~~powershell
ctest --test-dir build/windows-release -C Release --output-on-failure
~~~

当前测试目标包括：

- presentation_backend_configuration：真实呈现层的后端编译边界、状态更新、传统渲染及模式设置；另覆盖新用户默认 Vulkan、已有图形模式配置保留及图形模式解析结果持久化；
- 单图片背景：设置路径持久化、异步加载、裁剪填充、可读性遮罩、Vulkan 纹理上传和传统 Qt 回退绘制；
- theme_persistence_and_runtime：主题默认值与旧配置兼容、保存失败、运行时窗口状态保持及高 DPI 图标；
- note_color_modes：默认鲜明、旧配置迁移、损坏值与保存失败、设置联动、音级/打击乐映射、材质缓存失效及双后端版本传播；
- note_visual_semantics_and_rendering：音符语义、方角几何、踏板尾迹、动画及共享渲染；提供双后端截图、连续 Vulkan 压力和光栅性能诊断入口；
- soundfont_inspector：SoundFont 内容和格式检查；
- soundfont_setup：无音源启动引导、跳过、乐曲导入及播放内联错误；指定外部测试音源时另运行 soundfont_setup_audio，覆盖验证、持久化及配置后播放；
- visualization_domain：可视化投影、时间窗口、区间索引和场景数据；
- playback_session_transport：播放、暂停、停止、seek、事件代际和 transport 状态。
- metronome_timeline_and_readers：拍号、显式点击单位、附点速度、弱起、重复段落、MIDI format 2、重复标记的相位稳定性、可视化小节线与点击重音的一致性，以及丢帧后的节拍调度。
- metronome_fluidsynth_audio：使用真实 FluidSynth 和正式混音回调离线合成，验证点击音强弱、自然结束、音源切换，以及歌曲复音满载时连续点击和取消不抢占任何歌曲声部；配置 Windows FluidSynth DLL 时启用，无需音频设备。

节拍器点击资源 `assets/metronome.sf2` 已提交到源码并通过 Qt Resource 嵌入可执行文件。`scripts/generate-metronome-soundfont.py` 使用 Python 标准库生成原创采样，仅用于重建该资源，普通构建和运行不需要 Python。内置资源需要写入系统临时目录供 FluidSynth 读取，退出时自动清理；提取或准备失败时仅禁用节拍器并显示原因。

普通 PowerShell 如果找不到 ctest，请调用与 CMake 同目录的 ctest.exe，或使用 Visual Studio Developer PowerShell。

### 人工验收建议

| 场景 | 验收点 |
| --- | --- |
| MusicXML | 能打开、显示下落音符、读取 tempo/拍号/调号，并正常播放 |
| MIDI format 0/1 | 多轨道同时播放，program/channel 和 tempo 基本正确 |
| MIDI format 2 | 独立序列按规范串联，播放时长和轨道顺序合理 |
| 拖动进度 | 播放中释放后继续播放，暂停中释放后保持暂停，下一次播放从目标位置开始 |
| SoundFont | 无有效配置时启动引导可跳过；缺少音源时播放仅内联报错；SF2/SF3 可配置和切换，失败时保留原有选择 |
| 节拍器 | 小节首拍重音、变速同步，暂停和停止无声，关闭不截断钢琴音，切换音源后仍能发声 |
| 简谱条显隐 | 默认隐藏简谱条和黄线，音符在琴键顶部判定；播放和暂停时切换立即生效，切换图形模式后保持选择 |
| 设置持久化 | 重启后主题、音符色彩、刷新率、图形模式、简谱条显隐、标题栏模式和自定义音源路径仍可恢复 |
| Release 部署 | exe、Qt 平台插件、FluidSynth DLL 完整，发行包不含外部 SF2/SF3 音源 |

自动测试不替代人工听音验收；音频设备、系统音量和 FluidSynth 驱动初始化仍需在目标机器上确认。

## 已知限制与后续方向

- v0.4.0 中，设置文件无法写入时，启动音源引导仍可能关闭，所选路径仅在本次运行中生效；重启后可能需要重新配置。
- v0.4.0 中，音源文件临时移走后再恢复，播放可以恢复，但旧错误提示可能仍显示；在设置中重新加载音源可清除提示。
- 当前只验证 Windows x64 / MSVC；macOS/Linux 的 Qt 架构分支已预留，但没有同等完整的构建、部署和音频验收基线。
- 当前只有 FluidSynth 音频后端，不提供 Qt Multimedia 后端、外部 MIDI 硬件输出或音频文件导出。
- Vulkan 是否可用取决于显卡、驱动、Qt Vulkan 支持和运行环境。启动或设备初始化失败时程序会尝试回退传统 Qt；若图形设备在运行中丢失，建议重启后在设置中选择传统 Qt。
- 单图片背景当前只支持本地静态图片，不支持动态 WebP、多图轮播、视频、网络 URL 或背景音频；WebP 文件大小上限为 64 MB。原图不会复制到发行目录，移动或删除文件后需要在设置中重新选择。
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
  branding/                          Logo、横版字标与 Windows 应用图标
  metronome.sf2                       原创节拍器资源（嵌入可执行文件）
docs/
  images/midiplay-banner.svg          README 使用的深色底横版 Logo
  *.md                                主题、音符色彩等设计与开发文档
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
  infrastructure/resources/           项目音频资源设置模型
  presentation/                       Qt Widgets、设置窗口和渲染器
tests/                                呈现层、领域、transport 测试及部署样例
scripts/Build-Windows.ps1              Windows 环境检查、一键构建和发行校验
build.env.sample.psd1                  环境配置模板（本地实际配置不入 Git）
cmake/                                DLL 部署与发行目录依赖检查
CMakeLists.txt                        构建目标、资源复制和安装规则
CMakePresets.json                     Windows MSVC / Ninja 预设
vcpkg.json                            锁定基线的 FluidSynth / SF3 依赖清单
~~~

## 参与开发

提交代码前建议完成：

1. 使用与目标平台匹配的 Qt、编译器和 FluidSynth 运行时；
2. Debug 或 Release 构建通过；
3. ctest 全部通过；
4. 对涉及播放时序、seek、SoundFont 或渲染性能的改动补充对应测试或验收说明；
5. 保持领域层不依赖 Qt Widgets 和 FluidSynth 具体类型。

欢迎通过 [Issues](https://github.com/allenwu258/midi-play/issues) 反馈问题和建议。问题反馈请尽量附带：软件版本、操作系统、图形模式、输入文件格式、SoundFont 类型、复现步骤和相关日志。音频问题还应说明系统默认输出设备和 libfluidsynth-3.dll 的实际位置；界面问题可附运行截图。

Logo 与图标的使用规范见 [品牌资源说明](assets/branding/README.md)。

## 许可证

本项目采用 [MIT License](LICENSE)。发行包不包含第三方乐曲 SoundFont，用户自行选择的音源遵循各自的许可证或使用条款。仓库仅保留项目原创、程序化生成的节拍器资源 `assets/metronome.sf2`；Qt、FluidSynth 等第三方依赖仍遵循各自的许可证。
