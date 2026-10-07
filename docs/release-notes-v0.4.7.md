# MIDI Play v0.4.7

v0.4.7 adds a production-ready Vulkan video export pipeline for the Windows x64 portable release. It keeps the existing export dialog and output semantics while improving throughput and making bottlenecks observable.

## Highlights

- Vulkan offscreen MP4 export with a three-slot frame-in-flight pipeline.
- Fixed image-frame buffers and a bounded FFmpeg queue with backpressure, cancellation, and failure wake-up handling.
- GPU readback, CPU conversion, and encoding overlap without unbounded memory growth.
- Automatic NVIDIA `h264_nvenc` detection with software `libx264` fallback.
- Stable frame ordering, audio/video timeline synchronization, safe output commit, and MP4 integrity validation.
- Export diagnostics for render, readback, queue, encode, and hardware-utilization timing.

## Performance reference

On an RTX 5060 Laptop GPU, a 4K / 140-frame benchmark completed in about 1.9 seconds with NVENC and 2.6 seconds with `libx264`. Results depend on the selected resolution, frame rate, scene complexity, FFmpeg build, driver, and system load.

## Compatibility and setup

- Windows x64 portable package; Qt and Vulkan runtime files are deployed beside the executable.
- A compatible Vulkan driver is required for video export.
- FFmpeg is intentionally external and must be available through `PATH` or selected in the application settings.
- NVENC is enabled only when a compatible NVIDIA driver and FFmpeg encoder are available; otherwise the application uses software H.264.
- SoundFont files remain user-provided and are not included in the package.

## Validation

- Release build and Windows dependency-closure validation completed.
- Vulkan integration coverage: 8/8 cases passed.
- CTest: 14 tests passed; 2 environment-dependent tests skipped (`window_chrome_vulkan` and `video_encoder_roundtrip` when the required runtime is unavailable).
- CLI render and MP3 smoke tests passed for the packaged application.

## Known limitations

- Video export is unavailable in traditional Qt rendering mode or without Vulkan.
- The portable package does not include FFmpeg, NVIDIA drivers, or an NVENC runtime.
- Software H.264 fallback can be slower than real-time for high-resolution or complex scenes.
- macOS and Linux do not have the same release and audio acceptance baseline yet.

## Installation

1. Extract `midi-play-v0.4.7-windows-x64.zip` to a directory.
2. Keep the executable, DLLs, `platforms`, and license files together.
3. Start `midi_play.exe` and select a local SF2/SF3 SoundFont when prompted.
4. Configure FFmpeg in settings before using the video export tab.

For the implementation details and diagnostic workflow, see [视频导出性能优化复盘](video-export-performance-review.md).
