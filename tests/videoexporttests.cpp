#include "app/exporttimeline.h"
#include "app/audioexportservice.h"
#include "app/playerapplicationservice.h"
#include "app/settingsservice.h"
#include "app/videoexportservice.h"
#include "domain/music/musicdocument.h"
#include "infrastructure/encoding/ffmpegprobe.h"
#include "infrastructure/settings/qsettingsstore.h"
#include "presentation/exportdialog.h"
#include "presentation/mainwindow.h"

#include <QApplication>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
#include <QTabWidget>
#include <QTemporaryDir>

#include <atomic>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>
#include <vector>

#include <cstdio>
#include <cstdlib>

namespace {

void require(bool condition, const char* message)
{
    if (condition) return;
    std::fprintf(stderr, "FAILED: %s\n", message);
    std::exit(EXIT_FAILURE);
}

void testExportTimeline()
{
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
        const QByteArray bytes = decode(ffmpeg, {"-i", path, "-map", "0:a:0", "-f", "f32le",
                                            "-ar", "44100", "-ac", "2", "pipe:1"});
    require(bytes.size() % 8 == 0, "integration audio is complete stereo PCM");
    std::vector<float> samples(size_t(bytes.size()) / sizeof(float));
    std::memcpy(samples.data(), bytes.constData(), size_t(bytes.size()));
    return samples;
}

void compareWithAudioExport(const QString& ffmpeg, const QString& mp4, const QString& wav)
{
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

            // The reference calls the existing standalone export service.
            // Speed is represented as a tempo change in its input document,
            // since that service intentionally has no video rate parameter.
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

} // namespace

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    testExportTimeline();
    testFfmpegProbe();
    testFfmpegSettings();
    testUnifiedExportDialogTabs();
    if (app.arguments().contains(QStringLiteral("--integration"))) testVideoIntegration();
    return 0;
}
