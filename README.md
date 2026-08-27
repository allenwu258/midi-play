# MIDI Play

基于 Qt 6、MSVC 和 FluidSynth 的跨平台轻量级 MusicXML 播放器初版。

## 已实现

- MusicXML (`.xml` / `.musicxml`) 流式读取。
- 分层标准 MIDI 文件 reader（`.mid` / `.midi` / `.kar`；format 0/1/2、PPQN/SMPTE、Running Status、Meta/SysEx、按 port/channel 拆分、踏板和控制器归一化）。
- `score-partwise` 的 part、measure、tempo、note、rest、chord、grace、cue、backup、forward 基础语义。
- 统一 tick 时间线、tempo map 和 tick/time 双向转换。
- MusicXML `score-timewise` 自动转换为统一 `score-partwise` 语义入口。
- MusicXML 每个 staff/voice 独立维护游标，并使用跨小节余数累积降低 divisions 换算漂移。
- `MusicDocument` 提供全局 MeasureGrid、预计算 TempoMap 和稳定 NoteId。
- 默认 `midiSound-2025-1-14.sf2` 音源。
- FluidSynth SF2 动态适配器，不把 FluidSynth 类型泄漏到领域层。
- 播放、暂停、停止、进度拖动和旧音符 flush。
- Qt Widgets UI，解析在 QtConcurrent 工作线程执行。
- `--audio-test <sf2>` 以及 MusicXML/MIDI 路径命令行解析 smoke test。

MIDI format 2 的各个独立序列会按源轨道顺序串联到统一播放时间线；format 0/1 则保持轨道并行。SMPTE division 使用固定帧率换算，tempo meta-event 不会覆盖固定 tick 速率。

## 构建

在 Developer PowerShell for VS 2022 中执行：

```powershell
$env:QT_ROOT = 'C:/SDK/Qt/6.8.3/msvc2022_64'
$env:VCPKG_ROOT = 'C:/SDK/VC/vcpkg'

& "$env:VCPKG_ROOT/vcpkg.exe" install --triplet x64-windows

cmake --preset windows-msvc-debug
cmake --build --preset windows-msvc-debug

& "$env:QT_ROOT/bin/windeployqt.exe" --debug --compiler-runtime `
    'build/windows-msvc-debug/Debug/midi_play.exe'
```

CMake 会在可用时将默认 SF2 和 vcpkg 的 FluidSynth DLL 复制到输出目录。

## 运行

```powershell
build/windows-msvc-debug/Debug/midi_play.exe
```

也可以直接对 MusicXML 或 MIDI 做无界面解析 smoke test：

```powershell
build/windows-msvc-debug/Debug/midi_play.exe `
  'C:/Users/Vyas/Downloads/midi-files/Canon in D_211QUeDwFsn/211QUeDwFsn.musicxml'

build/windows-msvc-debug/Debug/midi_play.exe `
  'C:/Users/Vyas/Downloads/midi-files/Canon in D_211QUeDwFsn/3cdfa9914c7b42928694349744b8800b.mid'
```

测试 SF2 加载和单音输出：

```powershell
build/windows-msvc-debug/Debug/midi_play.exe --audio-test `
  'C:/Users/Vyas/projects/midi-play/midiSound-2025-1-14.sf2'
```

测试 MIDI reader：

```powershell
build/windows-msvc-debug/Debug/midi_play.exe --midi-test `
  'C:/Users/Vyas/Downloads/midi-files/Canon in D_211QUeDwFsn/3cdfa9914c7b42928694349744b8800b.mid'
```


## Qt 平台插件说明

Debug 部署必须包含：

```text
platforms/qwindowsd.dll
```

程序启动时会将可执行文件旁的 `platforms` 目录设置为 `QT_QPA_PLATFORM_PLUGIN_PATH`，避免从错误的 Qt 安装目录加载插件。若手动移动 exe，需要一起移动 `platforms`、Qt DLL 和 MSVC Debug runtime。

## 架构

```text
Qt UI
  -> PlayerApplicationService
  -> MusicReaderRegistry / MusicXmlReader / MusicDocument
  -> PlaybackModel / PlaybackContext / PlaybackEventsRenderer
  -> PlaybackController / PlaybackSession / PlaybackEventScheduler / PlaybackEventMap
  -> ThreadedPlaybackAudioService
  -> FluidSynthAudioService / SoundProfileRepository
  -> FluidSynthEngine / FluidSynth audio driver
```

当前引擎还包含：

- repeat segment 和 ending 的播放顺序展开。
- D.C./D.S./Segno/Coda/Fine 基础跳转和重复段 tempo-aware 时间映射。
- staff/voice 独立 playback track。
- 显式 NoteOn/NoteOff 事件流，避免由 UI 定时器生成 note-off。
- tie 合并、staccato、accent、tenuto、ghost articulation、dynamic/hairpin/pedal。
- ProgramChange、ControlChange、PitchBend、ChannelPressure 和 instrument change。
- 每轨道 PlaybackStateSnapshot，用于 seek 时恢复音色、控制器和跨目标位置的长音。
- mainStream/offStream 分离，并由独立 `PlaybackEventScheduler` 负责跨轨道时间游标。
- FluidSynth 后端使用其原生实时音频驱动；播放会话以软件单调时钟驱动事件窗口，并通过直接 MIDI API 提交事件。
- playback thread 与 audio service thread。
- `ProjectAudioSettings` JSON 持久化。

领域模型和播放状态不依赖 Qt Widgets 或 FluidSynth 具体类型，后续可注册 MIDI reader、替换音频后端或增加谱面视图。
