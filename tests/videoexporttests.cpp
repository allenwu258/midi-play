// 视频导出测试：验证时间线取整、应用校验、取消和临时文件保护。
#include "app/exporttimeline.h"
#include "app/audioexportservice.h"
#include "app/playerapplicationservice.h"
#include "app/settingsservice.h"
#include "app/videoexportservice.h"
#include "domain/music/musicdocument.h"
#include "domain/playback/playbackmodel.h"
#include "domain/visualization/playbackvisualizationprojector.h"
#include "infrastructure/encoding/ffmpegprobe.h"
#include "infrastructure/settings/qsettingsstore.h"
#include "presentation/exportdialog.h"
#include "presentation/mainwindow.h"

#include <QApplication>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
#include <QTabWidget>
#include <QTemporaryDir>

#include <atomic>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <deque>
#include <memory>
#include <vector>

#include <cstdio>
#include <cstdlib>

namespace {

void require(bool condition, const char* message)
{
    // 失败立即终止，避免继续操作可能未提交的临时输出。
    if (condition) return;
    std::fprintf(stderr, "FAILED: %s\n", message);
    std::exit(EXIT_FAILURE);
}

void testExportTimeline()
{
    // 覆盖倍率、尾音、无效参数及 44.1 kHz 与 CFR 帧数的精确映射。
    using midi_play::app::ExportTimeline;

    const ExportTimeline normal(1'000'000, 60, 100, 500);
    require(normal.valid(), "normal export timeline is valid");
    require(normal.frameCount() == 90, "timeline includes the fixed tail duration");
    require(normal.sampleCount() == 66'150, "timeline maps frames to 44.1 kHz samples");
    require(normal.musicPositionUs(30) == 500'000, "frame position uses absolute time");
    require(normal.eventSample(500'000) == 22'050, "event sample mapping is rate aware");

    const ExportTimeline doubleSpeed(1'000'000, 30, 200, 500);
    require(doubleSpeed.frameCount() == 30, "double speed halves musical output time");
    require(doubleSpeed.musicPositionUs(15) == 1'000'000,
            "double speed reaches one second at the middle frame");
    require(doubleSpeed.eventSample(1'000'000) == 22'050,
            "double speed maps source events to output samples");

    const ExportTimeline fractionalRate(1'000'001, 30, 150, 333);
    require(fractionalRate.valid(), "fractional video timeline is valid");
    require(fractionalRate.frameCount() == 30,
            "music speed is applied before frame quantization while tail stays real time");
    require(fractionalRate.videoDurationUs() == 1'000'000,
            "encoded duration is determined by complete CFR frames");
    require(fractionalRate.sampleCount() == 44'100,
            "audio sample count exactly matches the encoded video duration");
    require(fractionalRate.musicPositionUs(15) == 750'000,
            "video frame positions use the same speed ratio as audio events");

    // 精确帧边界不得因分步取整多出一帧；最大时长用整数不等式验证只取整一次。
    const ExportTimeline exactBoundary(1'000'000, 60, 120, 0);
    require(exactBoundary.frameCount() == 50,
            "rational durations on a frame boundary are rounded only once");
    require(exactBoundary.sampleCount() == 36'750, "exact boundary has no stray PCM samples");
    for (const int fps : {30, 60}) {
        for (const int rate : {20, 50, 100, 120, 150, 200}) {
            const ExportTimeline longTimeline(ExportTimeline::maximumMusicUs, fps, rate, 5000);
            require(longTimeline.valid(), "maximum-duration timeline fits in 64-bit arithmetic");
            require(longTimeline.sampleCount() == longTimeline.frameCount() * (44'100 / fps),
                    "long export has no cumulative audio/video drift");
            const qint64 denominator = qint64(rate) * 1'000'000;
            const qint64 numerator = (ExportTimeline::maximumMusicUs * 100 + 5'000'000LL * rate) * fps;
            require((longTimeline.frameCount() - 1) * denominator < numerator
                    && longTimeline.frameCount() * denominator >= numerator,
                    "frame count encloses the exact rational duration without an extra frame");
        }
    }

    require(!ExportTimeline(0, 60, 100, 500).valid(), "zero duration is rejected");
    require(!ExportTimeline(1'000'000, 24, 100, 500).valid(), "unsupported FPS is rejected");
    require(!ExportTimeline(1'000'000, 60, 10, 500).valid(), "unsupported rate is rejected");
}

void testFfmpegProbe()
{
    // 验证 FFmpeg 路径探测能区分缺失可执行文件与可用安装。
    const auto missing = midi_play::encoding::probeFfmpeg(
        QStringLiteral("C:/this/path/does/not/contain/ffmpeg"), false);
    require(!missing.valid && !missing.error.isEmpty(), "invalid FFmpeg path is rejected");

    const QString executable = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
    if (executable.isEmpty()) return;
    const auto result = midi_play::encoding::probeFfmpeg(executable, false);
    require(result.valid, "an available FFmpeg installation passes the probe");
    require(!result.version.isEmpty(), "FFmpeg probe reports a version");
}

void testFfmpegSettings()
{
    // 验证手动 FFmpeg 路径和使用方式写入设置后可被规范化恢复。
    QTemporaryDir directory;
    require(directory.isValid(), "temporary settings directory is available");
    const QString settingsPath = directory.filePath(QStringLiteral("settings.ini"));
    const QString selectedPath = directory.filePath(QStringLiteral("ffmpeg"));

    midi_play::app::SettingsService service(
        std::make_unique<midi_play::infrastructure::settings::QSettingsStore>(settingsPath));
    service.load();
    service.setFfmpegUsePath(false);
    service.setFfmpegPath(selectedPath);

    midi_play::infrastructure::settings::QSettingsStore persisted(settingsPath);
    const auto loaded = persisted.load(nullptr);
    require(!loaded.ffmpegUsePath, "manual FFmpeg mode is persisted");
    require(QFileInfo(loaded.ffmpegPath).absoluteFilePath() == QFileInfo(selectedPath).absoluteFilePath(),
            "manual FFmpeg directory is persisted as an absolute path");
}

void testUnifiedExportDialogTabs()
{
    // 验证统一导出对话框的音频/视频页签和默认导出类型。
    QTemporaryDir directory;
    require(directory.isValid(), "temporary UI settings directory is available");
    midi_play::app::VideoExportOptions defaults;
    defaults.outputPath = directory.filePath(QStringLiteral("score.mp4"));
    midi_play::presentation::ExportDialog dialog(
        directory.filePath(QStringLiteral("score.mid")), {}, defaults, 0, false,
        QStringLiteral("视频导出仅支持 Vulkan 图形模式"));
    auto* tabs = dialog.findChild<QTabWidget*>(QStringLiteral("exportTabs"));
    require(tabs != nullptr, "unified export dialog exposes its mode tabs");
    require(tabs->count() == 2 && tabs->tabText(0) == QStringLiteral("音频")
                && tabs->tabText(1) == QStringLiteral("视频"),
            "unified export dialog exposes audio and video tabs");
    require(dialog.exportType() == midi_play::presentation::ExportDialog::ExportType::Audio,
            "unified export dialog opens on the audio tab");
    tabs->setCurrentIndex(1);
    require(dialog.exportType() == midi_play::presentation::ExportDialog::ExportType::Video,
            "unified export dialog switches to the video tab");
}

// 可复用集成夹具：五个可辨识击声跨速度变化分布，可选反复验证展开后顺序。
std::shared_ptr<midi_play::music::MusicDocument> integrationDocument(bool repeat)
{
    using namespace midi_play::music;
    auto document = std::make_shared<MusicDocument>();
    document->setDuration(1920);
    document->tempos().push_back({0, 120.0, 0});
    document->tempos().push_back({960, 90.0, 0});
    Track track;
    track.id = QStringLiteral("video-test");
    track.channel = 0;
    track.program = 127;
    InstrumentChange preset;
    preset.channel = 0;
    preset.program = 127;
    preset.bankMsb = 1;
    track.instrumentChanges.push_back(preset);
    for (const Tick tick : {0, 120, 427, 823, 1667}) {
        NoteEvent note;
        note.start = tick;
        note.duration = 72;
        note.pitch = 60 + int(track.notes.size() % 2);
        note.velocity = 80 + int(track.notes.size() * 4);
        note.noteId = track.notes.size() + 1;
        note.program = 127;
        track.notes.push_back(note);
    }
    track.timeSignatures.push_back({0, 2, 4});
    document->tracks().push_back(track);
    if (repeat) {
        Measure first;
        first.number = 1; first.start = 0; first.duration = 960; first.repeatStart = true;
        Measure second;
        second.number = 2; second.start = 960; second.duration = 960;
        second.repeatEnd = true; second.repeatCount = 2;
        document->measures() = {first, second};
    }
    return document;
}

QByteArray decode(const QString& ffmpeg, const QStringList& arguments)
{
    // 在无交互模式运行 FFmpeg，并把标准输出作为结构化测试数据返回。
    QProcess process;
    process.start(ffmpeg, QStringList{"-v", "error", "-nostdin"} + arguments);
    require(process.waitForStarted(3000), "integration decoder starts");
    require(process.waitForFinished(30000), "integration decoder completes");
    require(process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0,
            "integration decoder succeeds");
    return process.readAllStandardOutput();
}

std::vector<float> decodeAudio(const QString& ffmpeg, const QString& path)
{
    // 将导出文件解码为 44.1 kHz 双声道 PCM，统一音频比较格式。
        const QByteArray bytes = decode(ffmpeg, {"-i", path, "-map", "0:a:0", "-f", "f32le",
                                            "-ar", "44100", "-ac", "2", "pipe:1"});
    require(bytes.size() % 8 == 0, "integration audio is complete stereo PCM");
    std::vector<float> samples(size_t(bytes.size()) / sizeof(float));
    std::memcpy(samples.data(), bytes.constData(), size_t(bytes.size()));
    return samples;
}

void compareWithAudioExport(const QString& ffmpeg, const QString& mp4, const QString& wav)
{
    // 将视频音频与独立 WAV 导出对齐比较，允许封装延迟但拒绝时间偏移和重复混音。
    const auto expected = decodeAudio(ffmpeg, wav);
    const auto decoded = decodeAudio(ffmpeg, mp4);
    require(decoded.size() >= expected.size() && decoded.size() - expected.size() < (1600 + 1024) * 2,
            "video audio adds only frame alignment and final AAC padding");
    double energy = 0, error = 0;
    for (size_t i = 0; i < expected.size(); ++i) {
        require(std::isfinite(decoded[i]), "integrated audio is finite");
        energy += double(expected[i]) * expected[i];
        error += std::pow(double(expected[i]) - decoded[i], 2);
    }
    if (!(energy > 0 && error / energy < 0.03)) {
        std::fprintf(stderr, "integrated audio relative squared error: %.6f\n", error / energy);
    }
    require(energy > 0 && error / energy < 0.03,
            "MP4 audio matches unchanged WAV export without shifts or overlapping copies");
}

void verifyMovingScene(const QString& ffmpeg, const QString& mp4, qint64 frames)
{
    // 抽取首帧和中间帧，确认 Vulkan 视频既非空白又随时间推进。
    const QByteArray pixels = decode(ffmpeg, {"-i", mp4, "-map", "0:v:0", "-vf",
        QStringLiteral("select=eq(n\\,0)+eq(n\\,%1)").arg(frames / 2),
        "-fps_mode", "passthrough", "-f", "rawvideo", "-pix_fmt", "rgb24", "pipe:1"});
    constexpr int frameBytes = 320 * 240 * 3;
    require(pixels.size() == frameBytes * 2, "two integrated video frames decode");
    int changed = 0;
    for (int i = 0; i < frameBytes; ++i) {
        changed += std::abs(int(static_cast<unsigned char>(pixels[i]))
                            - int(static_cast<unsigned char>(pixels[i + frameBytes]))) > 20;
    }
    require(changed > 300, "Vulkan export contains a moving rendered scene");
    for (int frame = 0; frame < 2; ++frame) {
        const auto begin = pixels.constData() + frame * frameBytes;
        const auto extrema = std::minmax_element(begin, begin + frameBytes,
            [](char a, char b) { return static_cast<unsigned char>(a) < static_cast<unsigned char>(b); });
        require(int(static_cast<unsigned char>(*extrema.second))
                    - int(static_cast<unsigned char>(*extrema.first)) > 50,
                "Vulkan scene has nonblank visual content");
    }
}

void testVideoIntegration()
{
    // 在多 FPS、多倍率及反复/节拍器组合下覆盖完整 Vulkan 到 FFmpeg 导出链路。
    const auto ffmpeg = midi_play::encoding::probeFfmpeg(
        QStandardPaths::findExecutable(QStringLiteral("ffmpeg")), false);
    require(ffmpeg.valid, "integration requires a usable FFmpeg installation");

    QTemporaryDir directory;
    require(directory.isValid(), "video integration output directory is available");
    midi_play::app::VideoExportOptions options;
    options.ffmpegExecutable = ffmpeg.executablePath;
    options.outputPath = directory.filePath(QStringLiteral("integration.mp4"));
    options.sourcePath = directory.filePath(QStringLiteral("input.mid"));
    options.soundFontPath = QStringLiteral(MIDI_PLAY_TEST_CLICK_SF2);
    options.scene.logicalSize = QSize(1280, 720);
    options.scene.outputSize = QSize(320, 240);
    options.tailMilliseconds = 123;

    for (const int fps : {30, 60}) {
        for (const int rate : {50, 100, 150, 200}) {
            const bool repeat = fps == 60;
            const auto document = integrationDocument(repeat);
            options.fps = fps;
            options.ratePercent = rate;
            options.includeMetronome = repeat;
            const auto result = midi_play::app::VideoExportService::exportDocument(document, options);
            if (result.status != midi_play::app::VideoExportStatus::Success) {
                std::fprintf(stderr, "video export error: %s\n", result.error.toUtf8().constData());
            }
            require(result.status == midi_play::app::VideoExportStatus::Success,
                    "Vulkan and FFmpeg export a multi-note MP4");
            require(result.frames > 0 && QFileInfo(options.outputPath).size() > 0,
                    "integration MP4 has frames and bytes");
            require(result.metrics.submittedFrames == result.frames
                        && result.metrics.encodedFrames == result.frames,
                    "concurrent pipeline submits and encodes every frame");
            require(result.metrics.peakInFlightFrames >= 2,
                    "concurrent pipeline keeps multiple Vulkan frames in flight");

            // 参考结果调用现有独立音频导出服务；该服务没有视频倍率参数，
            // 因此通过修改输入文档速度来表示倍率。
            auto reference = std::make_shared<midi_play::music::MusicDocument>(*document);
            for (auto& tempo : reference->tempos()) tempo.bpm *= rate / 100.0;
            midi_play::app::AudioExportOptions audio;
            audio.outputPath = directory.filePath(QStringLiteral("reference.wav"));
            audio.soundFontPath = options.soundFontPath;
            audio.format = midi_play::encoding::AudioFileFormat::Wav;
            audio.sampleRate = 44100;
            audio.tailMilliseconds = options.tailMilliseconds;
            audio.includeMetronome = options.includeMetronome;
            const auto exported = midi_play::app::AudioExportService::exportDocument(reference, audio);
            require(exported.status == midi_play::app::AudioExportStatus::Success,
                    "unchanged standalone audio export produces reference");
            compareWithAudioExport(ffmpeg.executablePath, options.outputPath, audio.outputPath);
            verifyMovingScene(ffmpeg.executablePath, options.outputPath, result.frames);
            std::fprintf(stdout, "PASS: Vulkan %d FPS, %d%%, repeat/metronome=%d\n", fps, rate, repeat);
        }
    }
}

void benchmarkVideoExport()
{
    // 测量不同输出分辨率下的编码、GPU 提交和图像回读耗时。
#if MIDI_PLAY_HAS_VULKAN
    using midi_play::presentation::visualization::VulkanOffscreenRenderer;
    using midi_play::presentation::visualization::VulkanRenderTicket;
    const auto document = integrationDocument(false);
    const auto ffmpeg = midi_play::encoding::probeFfmpeg(
        QStandardPaths::findExecutable(QStringLiteral("ffmpeg")), false);
    require(ffmpeg.valid, "benchmark requires FFmpeg");
    QTemporaryDir directory;
    require(directory.isValid(), "benchmark directory is available");
    for (const QSize size : {QSize(1920, 1080), QSize(3840, 2160)}) {
        midi_play::app::VideoExportOptions options;
        options.ffmpegExecutable = ffmpeg.executablePath;
        options.outputPath = directory.filePath(QStringLiteral("benchmark.mp4"));
        options.includeAudio = false;
        options.tailMilliseconds = 0;
        options.scene.outputSize = size;
        options.scene.font = QApplication::font();

        QElapsedTimer totalTimer;
        totalTimer.start();
        const auto result = midi_play::app::VideoExportService::exportDocument(document, options);
        if (result.status != midi_play::app::VideoExportStatus::Success)
            std::fprintf(stderr, "%s\n", result.error.toUtf8().constData());
        require(result.status == midi_play::app::VideoExportStatus::Success, "benchmark export succeeds");
        const qint64 totalMs = totalTimer.elapsed();
        const auto& metrics = result.metrics;
        std::fprintf(stdout,
            "BENCHMARK export %dx%d frames=%lld pipeline_ms=%lld finalize_ms=%lld total_ms=%lld\n",
            size.width(), size.height(), result.frames, metrics.videoPipelineMs, metrics.muxFinalizeMs, totalMs);

        auto scene = options.scene;
        scene.state.chart = midi_play::visualization::PlaybackVisualizationProjector().project(*document, 1, {});
        scene.state.durationUs = midi_play::playback::PlaybackModel(document).durationUs();
        VulkanOffscreenRenderer renderer;
        QString error;
        require(renderer.initialize(scene, &error), "benchmark renderer initializes");
        // 复用三张图像并预热每个 Vulkan slot，将稳定回读耗时与分配/着色器启动成本分离。
        QImage frames[3];
        for (auto& frame : frames) frame = QImage(size, QImage::Format_RGBA8888);
        std::deque<VulkanRenderTicket> pending;
        for (int i = 0; i < 3; ++i) {
            VulkanRenderTicket ticket;
            require(renderer.beginRender(i * 16'667, ticket, nullptr, &error), "warmup frame submits");
            pending.push_back(ticket);
        }
        for (int i = 0; i < 3; ++i) {
            auto ticket = pending.front();
            pending.pop_front();
            require(renderer.completeRender(ticket, frames[i], nullptr, &error), "warmup frame completes");
        }
        const auto initial = renderer.diagnostics();
        constexpr int frameCount = 120;
        int completed = 0;
        const auto completeNext = [&] {
            auto ticket = pending.front();
            pending.pop_front();
            require(renderer.completeRender(ticket, frames[completed % 3], nullptr, &error),
                    "benchmark readback completes");
            ++completed;
        };
        totalTimer.restart();
        for (int i = 0; i < frameCount; ++i) {
            if (pending.size() == 3) completeNext();
            VulkanRenderTicket ticket;
            require(renderer.beginRender(i * 16'667, ticket, nullptr, &error), "benchmark frame submits");
            pending.push_back(ticket);
        }
        while (!pending.empty()) completeNext();
        const qint64 renderMs = totalTimer.elapsed();
        const auto final = renderer.diagnostics();
        require(completed == frameCount, "benchmark completes all frame slots");
        std::fprintf(stdout,
            "BENCHMARK render %dx%d frames=%d elapsed_ms=%lld copy_ms=%.3f gpu_ms=%.3f\n",
            size.width(), size.height(), frameCount, renderMs,
            double(final.qimageCopyNs - initial.qimageCopyNs) / 1e6,
            double(final.gpuFrameNs - initial.gpuFrameNs) / 1e6);
    }
#endif
}

} // namespace

// 运行纯时间线/配置测试；--integration 和 --benchmark 追加完整导出与性能检查。
int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    testExportTimeline();
    testFfmpegProbe();
    testFfmpegSettings();
    testUnifiedExportDialogTabs();
    if (app.arguments().contains(QStringLiteral("--integration"))) testVideoIntegration();
    if (app.arguments().contains(QStringLiteral("--benchmark"))) benchmarkVideoExport();
    return 0;
}
