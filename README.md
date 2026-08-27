# MIDI Play

基于 Qt 6、MSVC 和 FluidSynth 的跨平台轻量级 MusicXML 播放器初版。

## 已实现

- MusicXML (`.xml` / `.musicxml`) 流式读取。
- `score-partwise` 的 part、measure、tempo、note、rest、chord、grace、cue、backup、forward 基础语义。
- 统一 tick 时间线、tempo map 和 tick/time 双向转换。
- 默认 `midiSound-2025-1-14.sf2` 音源。
- FluidSynth SF2 动态适配器，不把 FluidSynth 类型泄漏到领域层。
- 播放、暂停、停止、进度拖动和旧音符 flush。
- Qt Widgets UI，解析在 QtConcurrent 工作线程执行。
- `--audio-test <sf2>` 和 MusicXML 路径命令行解析 smoke test。

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

也可以直接对 MusicXML 做无界面解析 smoke test：

```powershell
build/windows-msvc-debug/Debug/midi_play.exe `
  'C:/Users/Vyas/Downloads/midi-files/Canon in D_211QUeDwFsn/211QUeDwFsn.musicxml'
```

测试 SF2 加载和单音输出：

```powershell
build/windows-msvc-debug/Debug/midi_play.exe --audio-test `
  'C:/Users/Vyas/projects/midi-play/midiSound-2025-1-14.sf2'
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
  -> MusicXmlReader / MusicDocument
  -> PlaybackSession / TempoClock / timeline
  -> FluidSynthEngine adapter
  -> FluidSynth audio driver
```

领域模型和播放状态不依赖 Qt Widgets 或 FluidSynth 具体类型，后续可注册 MIDI reader、替换音频后端或增加谱面视图。
