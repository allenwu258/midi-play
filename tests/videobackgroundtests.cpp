#include "domain/visualization/backgroundvideotimeline.h"
#include "infrastructure/media/videoframereader.h"
#include "infrastructure/encoding/backgroundaudioexport.h"
#include "infrastructure/settings/qsettingsstore.h"
#include "app/settingsservice.h"
#include "app/videoexportservice.h"
#include "app/playerapplicationservice.h"
#include "infrastructure/musicxml/musicxmlreader.h"
#include "presentation/visualization/videobackgroundcontroller.h"
#include "presentation/visualization/fallingnotesview.h"
#include "presentation/settings/settingsdialog.h"
#include "presentation/mainwindow.h"
#if MIDI_PLAY_HAS_VULKAN
#include "presentation/visualization/offscreen/vulkanoffscreenrenderer.h"
#include "presentation/visualization/fallingnotesvulkanwindow.h"
#include "presentation/visualization/vulkanscene.h"
#endif
#include <QApplication>
#include <QAudioOutput>
#include <QMediaPlayer>
#include <QCheckBox>
#include <QComboBox>
#include <QElapsedTimer>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QSlider>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <QToolButton>
#include <cstdio>
#include <functional>
#include <stdexcept>
#include <cmath>
#include <numbers>

namespace {
void require(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
bool waitUntil(const std::function<bool()>& condition)
{
    QElapsedTimer timer; timer.start();
    while (!condition() && timer.elapsed() < 8000) {
        QApplication::processEvents(); QThread::msleep(2);
    }
    return condition();
}
void testTimeline()
{
    using midi_play::visualization::backgroundVideoPosition;
    require(backgroundVideoPosition(0, 10'000, 3000) == 0, "initial frame");
    require(backgroundVideoPosition(2999, 10'000, 3000) == 2999, "first cycle end");
    require(backgroundVideoPosition(3000, 10'000, 3000) == 0, "loop wraps exactly");
    require(backgroundVideoPosition(7500, 10'000, 3000) == 1500, "multiple loops");
    require(backgroundVideoPosition(6000, 6000, 3000) == 2999, "terminal loop boundary freezes last frame");
    require(backgroundVideoPosition(7000, 6000, 3000) == 2999, "export tail remains frozen");
    require(backgroundVideoPosition(2000, 2000, 9000) == 1999, "long video is truncated");
    require(backgroundVideoPosition(-20, 2000, 9000) == 0, "negative input clamps");
    require(backgroundVideoPosition(500, 2000, 0) == 0, "unknown duration is safe");
}
void testSettings()
{
    using namespace midi_play;
    QTemporaryDir directory;
    const auto path = directory.filePath("settings.ini");
    { QSettings file(path, QSettings::IniFormat);
      file.setValue("Visualization/backgroundImageEnabled", true);
      file.setValue("Visualization/backgroundImagePath", "old.png"); }
    app::SettingsService service(std::make_unique<infrastructure::settings::QSettingsStore>(path));
    service.load();
    require(service.backgroundMode() == settings::BackgroundMode::Image, "legacy image mode migration");
    require(service.backgroundVideoMuted(), "video audio defaults to muted");
    service.setBackgroundVideoPath(QString::fromUtf8(MIDI_PLAY_TEST_VIDEO));
    service.setBackgroundMode(settings::BackgroundMode::Video);
    service.setBackgroundVideoMuted(false);
    require(service.activeBackgroundImagePath().isEmpty(), "video excludes image source");
    require(!service.activeBackgroundVideoPath().isEmpty(), "video path active");
    app::SettingsService restored(std::make_unique<infrastructure::settings::QSettingsStore>(path));
    restored.load();
    require(restored.backgroundMode() == settings::BackgroundMode::Video && !restored.backgroundVideoMuted(), "video preferences round trip");
    presentation::settings::SettingsDialog dialog(&restored, nullptr);
    auto* mode = dialog.findChild<QComboBox*>("backgroundModeCombo");
    auto* mute = dialog.findChild<QCheckBox*>("backgroundVideoMuted");
    require(mode && mode->count() == 3 && mode->currentIndex() == 2, "three background choices");
    require(mute && !mute->isHidden() && !mute->isChecked(), "video audio control restores preference");
    mute->setChecked(true);
    require(restored.backgroundVideoMuted(), "audio control updates settings");
    mode->setCurrentIndex(0);
    require(restored.activeBackgroundVideoPath().isEmpty() && !restored.backgroundVideoPath().isEmpty(), "disabling preserves video selection");
    require(mute->isHidden(), "video audio control hides outside video mode");
}
void testVideoTrackDuration()
{
    using namespace midi_play;
    const QDir fixtures = QFileInfo(QString::fromUtf8(MIDI_PLAY_TEST_VIDEO)).dir();
    media::VideoFrameReader reader;
    QString error;
    require(reader.open(fixtures.filePath("background-long-audio.mkv"), 256, &error), qPrintable(error));
    const qint64 duration = reader.metadata().durationUs;
    // Millisecond time bases round the last packet duration by at most 1 ms.
    require(std::abs(duration - 2'000'000) <= 1000, "video duration excludes longer audio track");
    const auto firstCycle = reader.frameAt(500'000, &error).image;
    const auto loopPosition = midi_play::visualization::backgroundVideoPosition(duration + 500'000, 8'000'000, duration);
    require(reader.frameAt(loopPosition, &error).image == firstCycle, "MKV loops on the video track endpoint");
}
void testInvalidBackgroundModeMigration()
{
    using namespace midi_play;
    for (const bool legacyEnabled : {false, true}) {
        for (const QVariant raw : {QVariant(), QVariant(0), QVariant(1), QVariant(2),
                                  QVariant(-1), QVariant(99), QVariant("broken")}) {
            QTemporaryDir directory;
            const auto path = directory.filePath("settings.ini");
            {
                QSettings file(path, QSettings::IniFormat);
                file.setValue("Visualization/backgroundImageEnabled", legacyEnabled);
                file.setValue("Visualization/backgroundImagePath", "old.png");
                if (raw.isValid()) file.setValue("Visualization/backgroundMode", raw);
            }
            infrastructure::settings::QSettingsStore store(path);
            QString warning;
            auto settings = store.load(&warning);
            bool converted = false;
            const int value = raw.toInt(&converted);
            const bool valid = raw.isValid() && converted && value >= 0 && value <= 2;
            const auto expected = valid ? static_cast<midi_play::settings::BackgroundMode>(value)
                : legacyEnabled ? midi_play::settings::BackgroundMode::Image : midi_play::settings::BackgroundMode::None;
            require(settings.backgroundMode == expected, "invalid mode preserves legacy setting; valid modes win");
            require(warning.isEmpty() == (!raw.isValid() || valid), "only invalid present modes report warnings");
            require(settings.backgroundImagePath == "old.png", "migration retains image path");
            require(store.save(settings, &warning), "migrated settings save");
            require(store.load(&warning).backgroundMode == expected && warning.isEmpty(), "migration remains stable after save");
        }
    }
}
void testCoarseTimeBaseSeeking()
{
    using namespace midi_play;
    const QDir fixtures = QFileInfo(QString::fromUtf8(MIDI_PLAY_TEST_VIDEO)).dir();
    media::VideoFrameReader reader;
    QString error;
    require(reader.open(fixtures.filePath("background-coarse-timebase.mp4"), 256, &error), qPrintable(error));
    for (const qint64 target : {1'300'000, 1'499'999, 1'500'000, 2'999'999, 1'300'000, 0}) {
        const auto frame = reader.frameAt(target, &error);
        require(!frame.image.isNull(), qPrintable(error));
        require(frame.positionUs == (target / 500'000) * 500'000,
            "coarse time base selects the containing frame, including reverse seeks");
    }
}
void testDecoderAndAsync()
{
    using namespace midi_play;
    const QString path = QString::fromUtf8(MIDI_PLAY_TEST_VIDEO);
    media::VideoFrameReader reader;
    QString error;
    require(reader.open(path, 256, &error), qPrintable(error));
    require(reader.metadata().durationUs == 2'000'000 && reader.metadata().hasAudio, "fixture metadata");
    const auto first = reader.frameAt(0, &error);
    require(!first.image.isNull() && first.image.width() == 256, "bounded RGBA decode");
    const auto middle = reader.frameAt(1'250'000, &error);
    require(middle.positionUs <= 1'250'000 && middle.positionUs >= 1'208'000, "seek chooses containing presentation frame");
    require(middle.image != first.image, "video advances");
    const auto back = reader.frameAt(0, &error);
    require(back.image == first.image, "reverse seek is deterministic");
    const auto end = reader.frameAt(1'999'999, &error);
    require(end.positionUs > 1'900'000 && !end.image.isNull(), "B-frame decoder drains terminal frames");
    require(reader.frameAt(1'999'999, &error).image.cacheKey() == end.image.cacheKey(), "paused frame reuses pixels");
    require(reader.frameAt(0, &error).image == first.image, "loop seeks after EOF");
    std::atomic_bool canceled {true};
    media::VideoFrameReader canceledReader;
    require(!canceledReader.open(path, 256, &error, &canceled), "canceled input does not decode");
    media::VideoFrameReader missing;
    require(!missing.open(path + ".missing", 256, &error) && !error.isEmpty(), "missing resource reports error");

    presentation::visualization::VideoBackgroundController controller;
    QImage shown;
    int frames = 0;
    QObject::connect(&controller, &presentation::visualization::VideoBackgroundController::frameReady,
        &controller, [&](const QImage& frame) { shown = frame; ++frames; });
    controller.setSource(path, 256);
    controller.synchronize(0, 6'000'000, false, 100);
    require(waitUntil([&] { return !shown.isNull(); }), "async first frame");
    require(!controller.findChild<QMediaPlayer*>(), "muted video never creates an audio player");
    for (int i = 0; i < 80; ++i) controller.synchronize(i * 71'123, 6'000'000, false, 100, true);
    controller.synchronize(1'250'000, 6'000'000, false, 100, true);
    require(waitUntil([&] { return shown == middle.image; }), "rapid scrub publishes latest target");
    controller.synchronize(6'000'000, 6'000'000, false, 100, true);
    require(waitUntil([&] { return shown == end.image; }), "completion holds final video frame");
    controller.synchronize(0, 6'000'000, false, 100, true);
    require(waitUntil([&] { return shown == first.image; }), "replay resets video");
    if (QCoreApplication::arguments().contains("--audio")) {
        controller.setMuted(false);
        auto* audio = controller.findChild<QMediaPlayer*>();
        auto* output = controller.findChild<QAudioOutput*>();
        require(audio && output, "unmuting creates the audio output");
        output->setVolume(0); // Exercise the real backend without playing a test tone.
        // LoadedMedia can immediately advance to BufferingMedia after the
        // controller positions the paused player. Test readiness, not that edge.
        require(waitUntil([&] { return audio->duration() > 0 && audio->hasAudio(); }), "video audio loads");
        controller.synchronize(0, 6'000'000, true, 150, true);
        require(waitUntil([&] { return audio->playbackState() == QMediaPlayer::PlayingState; }), "video audio follows play");
        require(audio->playbackRate() == 1.5 && audio->activeVideoTrack() == -1, "audio rate follows music without duplicate video decode");
        controller.synchronize(2'050'000, 6'000'000, true, 150, true);
        require(waitUntil([&] { return audio->position() < 200; }), "original audio rewinds on video loop");
        controller.synchronize(6'000'000, 6'000'000, false, 150, true);
        require(waitUntil([&] { return audio->playbackState() != QMediaPlayer::PlayingState; }), "completion stops original audio");
        controller.synchronize(0, 6'000'000, true, 150, true);
        require(waitUntil([&] { return audio->position() < 200 && audio->playbackState() == QMediaPlayer::PlayingState; }),
            "replay restarts original audio");
        controller.synchronize(1'000'000, 6'000'000, false, 150, true);
        require(waitUntil([&] { return audio->playbackState() != QMediaPlayer::PlayingState; }), "scrub preview silences audio");
        controller.setMuted(true);
        require(audio->source().isEmpty(), "muting unloads original audio");
    }
    controller.synchronize(1'500'000, 6'000'000, false, 100, true);
    controller.setSource({}, 256);
    const int before = frames;
    QElapsedTimer settle; settle.start();
    waitUntil([&] { return settle.elapsed() > 100; });
    require(frames == before, "cleared source rejects in-flight frames");

    presentation::visualization::FallingNotesView view;
    view.setGraphicsMode(settings::GraphicsMode::Traditional);
    view.setTransportPosition(0, 6'000'000);
    view.setBackgroundImageMaximumDimension(256);
    view.setBackgroundVideoPath(path);
    require(waitUntil([&] { return view.exportBackground() == first.image; }), "view receives initial video");
    view.previewPosition(1'250'000);
    view.setTransportPosition(500'000, 6'000'000);
    require(waitUntil([&] { return view.exportBackground() == middle.image; }), "slider preview overrides incoming transport samples");
    require(view.exportState().transportPositionUs == 1'250'000, "notes and video share preview time");
    view.setTransportPosition(1'250'000, 6'000'000);
    view.endPositionPreview();
    view.setBackgroundVideoPath({});
    require(view.exportBackground().isNull(), "disabling video clears the rendered frame");
}
void testAudioExport()
{
    const auto ffmpeg = QStandardPaths::findExecutable("ffmpeg");
    if (ffmpeg.isEmpty()) { std::puts("SKIP: optional external FFmpeg audio integration"); return; }
    QTemporaryDir directory;
    QString pcm, error;
    midi_play::media::VideoFrameReader reader;
    require(reader.open(QString::fromUtf8(MIDI_PLAY_TEST_VIDEO), 256, &error), qPrintable(error));
    require(midi_play::encoding::mixBackgroundVideoAudio(ffmpeg, QString::fromUtf8(MIDI_PLAY_TEST_VIDEO),
        reader.metadata(), 5'000'000, 200, 44100, 132300, directory.path(), pcm, nullptr, &error), qPrintable(error));
    QFile file(pcm); require(file.open(QIODevice::ReadOnly), "read mixed audio");
    const auto data = file.readAll();
    require(data.size() == 132300 * 8, "mixed audio has exact output duration");
    const auto* samples = reinterpret_cast<const float*>(data.constData());
    double audible = 0, tail = 0;
    for (int i = 0; i < 44100; ++i) audible += std::abs(samples[i]);
    for (int i = 120000 * 2; i < 132300 * 2; ++i) tail += std::abs(samples[i]);
    require(audible > 1 && tail < .001, "background audio stops before export tail");
}
void testAudioTimeOrigin()
{
    using namespace midi_play;
    const auto path = QFileInfo(QString::fromUtf8(MIDI_PLAY_TEST_VIDEO)).dir().filePath("background-delayed-video.mp4");
    media::VideoFrameReader reader;
    QString error;
    require(reader.open(path, 256, &error), qPrintable(error));
    const auto metadata = reader.metadata();
    require(metadata.startTimeUs == 1'000'000 && metadata.mediaStartTimeUs == 0,
        "video and container origins remain distinct");
    require(metadata.audioStartUs == -1'000'000 && metadata.audioEndUs == 2'000'000,
        "audio interval is relative to video origin");
    if (QCoreApplication::arguments().contains("--audio")) {
        presentation::visualization::VideoBackgroundController controller;
        QImage frame;
        QObject::connect(&controller, &presentation::visualization::VideoBackgroundController::frameReady,
            &controller, [&](const QImage& image) { frame = image; });
        controller.setSource(path, 256);
        controller.synchronize(0, 6'000'000, false, 100, true);
        require(waitUntil([&] { return !frame.isNull(); }), "offset video loads");
        controller.setMuted(false);
        auto* player = controller.findChild<QMediaPlayer*>();
        controller.findChild<QAudioOutput*>()->setVolume(0);
        require(waitUntil([&] { return player->duration() > 0; }), "offset audio loads");
        controller.synchronize(500'000, 6'000'000, false, 100, true);
        require(player->position() == 1500, "Qt audio uses the same source time as video");
    }
    const auto ffmpeg = QStandardPaths::findExecutable("ffmpeg");
    if (ffmpeg.isEmpty()) return;
    QTemporaryDir directory;
    QString pcm;
    require(encoding::mixBackgroundVideoAudio(ffmpeg, path, metadata, 2'000'000, 100, 44100,
        88200, directory.path(), pcm, nullptr, &error), qPrintable(error));
    QFile output(pcm);
    require(output.open(QIODevice::ReadOnly), "read offset audio");
    const auto bytes = output.readAll();
    const auto* samples = reinterpret_cast<const float*>(bytes.constData());
    const auto energy = [&](double frequency) {
        double real = 0, imaginary = 0;
        for (int i = 4410; i < 22050; ++i) {
            const double phase = 2 * std::numbers::pi * frequency * i / 44100;
            real += samples[2 * i] * std::cos(phase);
            imaginary += samples[2 * i] * std::sin(phase);
        }
        return real * real + imaginary * imaginary;
    };
    require(energy(880) > 100 * energy(440), "export drops the audio preceding the first video frame");
}
void testAudioLoopEvents()
{
    if (!QCoreApplication::arguments().contains("--audio")) return;
    using namespace midi_play;
    const auto path = QFileInfo(QString::fromUtf8(MIDI_PLAY_TEST_VIDEO)).dir().filePath("background-short-audio.mp4");
    presentation::visualization::VideoBackgroundController controller;
    QImage frame;
    QObject::connect(&controller, &presentation::visualization::VideoBackgroundController::frameReady,
        &controller, [&](const QImage& image) { frame = image; });
    controller.setSource(path, 256);
    controller.synchronize(0, 8'000'000, false, 100, true);
    require(waitUntil([&] { return !frame.isNull(); }), "short audio video loads");
    controller.setMuted(false);
    auto* player = controller.findChild<QMediaPlayer*>();
    controller.findChild<QAudioOutput*>()->setVolume(0);
    require(waitUntil([&] { return player->hasAudio(); }), "short audio track loads");
    controller.synchronize(200'000, 8'000'000, true, 100, true);
    require(waitUntil([&] { return player->playbackState() == QMediaPlayer::PlayingState; }), "first audio cycle plays");
    controller.synchronize(2'000'000, 8'000'000, true, 100, true);
    require(player->playbackState() != QMediaPlayer::PlayingState, "short audio tail stays silent");
    controller.synchronize(4'200'000, 8'000'000, true, 100, true);
    require(waitUntil([&] { return player->playbackState() == QMediaPlayer::PlayingState; }), "next audio cycle starts");
    // Deliberately deliver the previous cycle's completion after the loop seek.
    player->mediaStatusChanged(QMediaPlayer::EndOfMedia);
    player->pause();
    controller.synchronize(4'250'000, 8'000'000, true, 100);
    require(waitUntil([&] { return player->playbackState() == QMediaPlayer::PlayingState; }), "late EOF cannot suppress the new cycle");
    controller.synchronize(8'000'000, 8'000'000, false, 100, true);
    require(player->playbackState() != QMediaPlayer::PlayingState, "music endpoint always silences audio");
    controller.setSource(QString::fromUtf8(MIDI_PLAY_TEST_VIDEO), 256);
    player->errorOccurred(QMediaPlayer::ResourceError, "late error from previous source");
    controller.synchronize(0, 8'000'000, true, 100, true);
    require(waitUntil([&] {
        for (auto* candidate : controller.findChildren<QMediaPlayer*>())
            if (candidate != player && candidate->playbackState() == QMediaPlayer::PlayingState) return true;
        return false;
    }), "old source errors cannot poison replacement audio");
    controller.setMuted(true);
}
void testContinuousScrubbing()
{
    using namespace midi_play;
    const auto arguments = QCoreApplication::arguments();
    const int sourceOption = arguments.indexOf("--scrub-video");
    const QString path = sourceOption >= 0 && sourceOption + 1 < arguments.size()
        ? arguments[sourceOption + 1] : QString::fromUtf8(MIDI_PLAY_TEST_VIDEO);
    presentation::visualization::FallingNotesView view;
    view.setGraphicsMode(settings::GraphicsMode::Traditional);
    view.setTransportPosition(0, 12'000'000);
    view.setBackgroundImageMaximumDimension(256);
    view.setBackgroundVideoPath(path);
    require(waitUntil([&] { return !view.exportBackground().isNull(); }), "scrub source loads");
    auto* controller = view.findChild<presentation::visualization::VideoBackgroundController*>();
    int frames = 0, requests = 0;
    QObject::connect(controller, &presentation::visualization::VideoBackgroundController::frameReady,
        &view, [&](const QImage&) { ++frames; });
    QTimer updates;
    QObject::connect(&updates, &QTimer::timeout, &view, [&] {
        view.previewPosition(5'000'000 + (requests++ % 120) * 40'000);
    });
    QElapsedTimer elapsed; elapsed.start();
    updates.start(1);
    require(waitUntil([&] { return elapsed.elapsed() >= 2000; }), "continuous scrub completes");
    updates.stop();
    std::printf("Continuous scrub: %d requests, %d frames during dragging\n", requests, frames);
    require(requests > 10 && frames > 1, "continuous dragging publishes frames before release");
    media::VideoFrameReader expected;
    QString error;
    require(expected.open(path, 256, &error), qPrintable(error));
    const auto finalFrame = expected.frameAt(750'000, &error).image;
    view.previewPosition(750'000);
    require(waitUntil([&] { return view.exportBackground() == finalFrame; }), "scrub settles at exact latest target");
    view.setBackgroundVideoPath({});
}
void testSeekAcknowledgment()
{
    using namespace midi_play;
    app::PlayerApplicationService service;
    presentation::MainWindow window(&service, nullptr);
    auto* view = window.findChild<presentation::visualization::FallingNotesView*>();
    auto* slider = window.findChild<QSlider*>("positionSlider");
    require(view && slider, "transport preview widgets exist");
    view->setGraphicsMode(settings::GraphicsMode::Traditional);
    service.positionChanged(500'000, 6'000'000, 0);
    slider->setSliderDown(true);
    slider->setSliderPosition(slider->maximum() / 2);
    require(view->exportState().transportPositionUs == 3'000'000, "drag previews target");
    slider->setSliderDown(false);
    service.positionChanged(600'000, 6'000'000, 0);
    require(view->exportState().transportPositionUs == 3'000'000, "release ignores queued stale sample");
    service.positionChanged(3'000'000, 6'000'000, 0);
    service.positionChanged(3'100'000, 6'000'000, 0);
    require(view->exportState().transportPositionUs == 3'100'000, "acknowledged seek resumes transport updates");
}
void testStopAtEndpoint()
{
    using namespace midi_play;
    app::PlayerApplicationService service;
    presentation::MainWindow window(&service, nullptr);
    auto* stop = window.findChild<QToolButton*>("stopButton");
    require(stop != nullptr, "stop control exists");
    service.positionChanged(2'000'000, 2'000'000, 0);
    service.playbackStateChanged(playback::State::Stopped);
    require(stop->isEnabled(), "natural endpoint permits explicit reset");
    service.positionChanged(0, 2'000'000, 0);
    require(!stop->isEnabled(), "stopped at zero disables redundant reset");
    service.positionChanged(500'000, 2'000'000, 0);
    require(stop->isEnabled(), "seeking in stopped state re-enables reset");
    service.playbackStateChanged(playback::State::Playing);
    require(stop->isEnabled(), "replay keeps stop available");
}
#if MIDI_PLAY_HAS_VULKAN
void testOffscreen()
{
    using namespace midi_play::presentation::visualization;
    ExportSceneConfig config;
    config.backgroundVideoPath = QString::fromUtf8(MIDI_PLAY_TEST_VIDEO);
    config.backgroundMaximumDimension = 256;
    config.outputSize = {640, 360};
    config.state.durationUs = 6'000'000;
    config.state.visualEffectsEnabled = false; // Compare background pixels without the animated strike line.
    VulkanOffscreenRenderer renderer;
    QString error;
    require(renderer.initialize(config, &error), qPrintable(error));
    QImage first, middle, loop, end, tail;
    require(renderer.render(0, first, nullptr, &error), qPrintable(error));
    require(renderer.render(1'250'000, middle, nullptr, &error), qPrintable(error));
    require(first != middle, "offscreen output contains changing video frames");
    require(renderer.render(2'000'000, loop, nullptr, &error) && first == loop, "offscreen loops deterministically");
    require(renderer.render(6'000'000, end, nullptr, &error), qPrintable(error));
    require(renderer.render(6'500'000, tail, nullptr, &error) && end == tail, "offscreen tail freezes background");
}
void testVideoExport()
{
    using namespace midi_play;
    const auto ffmpeg = QStandardPaths::findExecutable("ffmpeg");
    require(!ffmpeg.isEmpty(), "full video integration requires FFmpeg");
    const QDir fixtures = QFileInfo(QString::fromUtf8(MIDI_PLAY_TEST_VIDEO)).dir();
    const auto loaded = musicxml::MusicXmlReader().read(fixtures.filePath("deployment.musicxml"));
    require(loaded.ok(), qPrintable(loaded.error));
    for (auto& tempo : loaded.document->tempos()) tempo.bpm = 40; // Three video cycles.
    QTemporaryDir output;
    app::VideoExportOptions options;
    options.ffmpegExecutable = ffmpeg;
    options.scene.backgroundVideoPath = QString::fromUtf8(MIDI_PLAY_TEST_VIDEO);
    options.scene.backgroundMaximumDimension = 256;
    options.scene.outputSize = {640, 360};
    options.soundFontPath = fixtures.filePath("../../assets/metronome.sf2");
    options.fps = 30;
    options.ratePercent = 200;
    options.tailMilliseconds = 500;
    // The service validates the completed media's streams, frame count and
    // duration. Exercise both background-only audio and the musical mix.
    for (int variant = 0; variant < 3; ++variant) {
        options.outputPath = output.filePath(QStringLiteral("background-%1.mp4").arg(variant));
        options.includeAudio = variant == 2;
        options.backgroundVideoMuted = variant == 0;
        const auto result = app::VideoExportService::exportDocument(loaded.document, options);
        require(result.status == app::VideoExportStatus::Success, qPrintable(result.error));
        require(result.frames == 105 && QFileInfo(options.outputPath).size() > 1000,
            "full export includes three video loops and a frozen tail");
    }
}
void testRealtimeVulkan()
{
    using namespace midi_play;
    using namespace presentation::visualization;
    FallingNotesView view;
    view.resize(960, 640);
    view.setVisualEffectsEnabled(false);
    view.setTransportPosition(0, 6'000'000);
    view.setBackgroundImageMaximumDimension(256);
    view.setBackgroundVideoPath(QString::fromUtf8(MIDI_PLAY_TEST_VIDEO));
    view.setGraphicsMode(settings::GraphicsMode::VulkanExperimental);
    view.show();
    require(waitUntil([&] { return view.vulkanReady() && !view.exportBackground().isNull(); }), "live Vulkan video initializes");
    FallingNotesVulkanWindow* window = nullptr;
    for (auto* candidate : QGuiApplication::allWindows())
        if (auto* vulkan = qobject_cast<FallingNotesVulkanWindow*>(candidate)) window = vulkan;
    require(window && window->supportsGrab(), "live Vulkan readback available");
    const auto initial = window->grab();
    const auto device = window->device();
    const auto firstPixels = view.exportBackground();
    // Exercise repeated same-size uploads on swapchain images without replacing
    // the window/device; transport stays paused so pixel comparisons are stable.
    for (int frame = 1; frame <= 24; ++frame) {
        const auto previous = view.exportBackground().cacheKey();
        view.previewPosition(frame * 50'000);
        require(waitUntil([&] { return view.exportBackground().cacheKey() != previous; }), "live video advances during scrub");
        require(!window->grab().isNull(), "live texture update renders");
    }
    require(window->grab() != initial && window->device() == device, "live frames change on the original Vulkan device");
    view.previewPosition(2'000'000);
    require(waitUntil([&] { return view.exportBackground() == firstPixels; }), "live video loops to first frame");
    require(window->grab() == initial, "live texture upload restores identical loop pixels");
    view.setGraphicsMode(settings::GraphicsMode::Traditional);
    require(view.exportBackground() == firstPixels, "backend switch preserves video frame");
}
#endif
}

int main(int argc, char** argv)
{
    QApplication application(argc, argv);
    try {
        testTimeline(); testSettings(); testInvalidBackgroundModeMigration(); testVideoTrackDuration(); testCoarseTimeBaseSeeking(); testDecoderAndAsync(); testContinuousScrubbing(); testAudioExport(); testAudioTimeOrigin(); testAudioLoopEvents(); testSeekAcknowledgment(); testStopAtEndpoint();
#if MIDI_PLAY_HAS_VULKAN
        if (application.arguments().contains("--vulkan")) testOffscreen();
        if (application.arguments().contains("--integration")) testVideoExport();
        if (application.arguments().contains("--window")) testRealtimeVulkan();
#endif
        std::puts("Video background tests passed");
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FAILED: %s\n", error.what()); return 1;
    }
}
