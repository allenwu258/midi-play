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
- 导入完成后由 `MusicAnalyzer` 生成 WrittenPitch、调内 degree、Raw/Grid 双时间、和弦组、延音/连音组、鼓组映射、量化网格、调性置信度、三连音候选、swing 比例和音游 lane。
- 默认 `assets/midisound.sf2` 音源。
- FluidSynth SF2 动态适配器，不把 FluidSynth 类型泄漏到领域层。
- 播放、暂停、停止、进度拖动和旧音符 flush。
- Qt Widgets UI，解析在 QtConcurrent 工作线程执行。
- 只读播放可视化：音符下落、长音/踏板尾段、固定触发线、真实钢琴键、鼓轨、简谱、小节/节拍、歌词和标记。
- `VisualChart` 不可变播放视图模型，MusicXML/MIDI 共用反复展开、tempo-aware 时间投影和稳定实例 ID。
- 带 subtree max-end 的平衡区间索引，seek 后可直接查询五秒可见窗口，无需逐帧扫描全谱。
- 60 Hz `QWidget + QPainter` 绘制仅采样权威 transport 位置，不使用 UI 定时器累加播放时间。
- `--audio-test <sf2>` 以及 MusicXML/MIDI 路径命令行解析 smoke test。

MIDI format 2 的各个独立序列会按源轨道顺序串联到统一播放时间线；format 0/1 则保持轨道并行。SMPTE division 使用固定帧率换算，tempo meta-event 不会覆盖固定 tick 速率。

## 构建

构建前需要配置以下环境变量：

- `QT_ROOT`：Qt MSVC 套件根目录，目录下应包含 `bin/windeployqt.exe`。
- `VCPKG_ROOT`：vcpkg 根目录，目录下应包含 `vcpkg.exe`。

在 Developer PowerShell for VS 2022 中，从项目根目录执行：

```powershell
if (-not (Test-Path "$env:QT_ROOT/bin/windeployqt.exe")) {
    throw 'QT_ROOT 未指向有效的 Qt MSVC 套件'
}
if (-not (Test-Path "$env:VCPKG_ROOT/vcpkg.exe")) {
    throw 'VCPKG_ROOT 未指向有效的 vcpkg 根目录'
}

& "$env:VCPKG_ROOT/vcpkg.exe" install --triplet x64-windows

cmake --preset windows-msvc-debug `
    -DCMAKE_PREFIX_PATH="$env:QT_ROOT"
cmake --build --preset windows-msvc-debug

& "$env:QT_ROOT/bin/windeployqt.exe" --debug --compiler-runtime `
    'build/windows-msvc-debug/Debug/midi_play.exe'
```

CMake 会在可用时将默认 SF2 复制到输出目录的 `assets` 子目录，并将 vcpkg 的 FluidSynth DLL 复制到可执行文件同级目录。

## 运行

```powershell
$midiPlayExe = 'build/windows-msvc-debug/Debug/midi_play.exe'
& $midiPlayExe
```

以下示例使用 `MIDI_PLAY_TEST_DATA` 指向包含测试 MusicXML 和 MIDI 文件的目录：

```powershell
$env:MIDI_PLAY_TEST_DATA = '<测试数据目录>'

& $midiPlayExe "$env:MIDI_PLAY_TEST_DATA/example.musicxml"
& $midiPlayExe "$env:MIDI_PLAY_TEST_DATA/example.mid"
```

测试 SF2 加载和单音输出：

```powershell
& $midiPlayExe --audio-test 'assets/midisound.sf2'
```

测试 MIDI reader：

```powershell
& $midiPlayExe --midi-test "$env:MIDI_PLAY_TEST_DATA/example.mid"
```

生成固定播放位置的离屏可视化帧：

```powershell
& $midiPlayExe --render-test `
  "$env:MIDI_PLAY_TEST_DATA/example.musicxml" `
  'build/visualization.png' `
  10000000 1280 720
```

最后三个参数依次为播放位置（微秒）、输出宽度和输出高度；宽高可省略，默认 `1280x720`。

运行可视化领域测试：

```powershell
ctest --test-dir build/windows-msvc-debug -C Debug --output-on-failure
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
  -> PlaybackVisualizationProjector / immutable VisualChart / VisibleNoteIndex
  -> FallingNotesView / SceneLayoutEngine / FallingNotesRenderer
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
