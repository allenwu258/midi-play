#include "domain/playback/iplaybackaudioservice.h"
#include "domain/playback/playbacksession.h"
#include "domain/playback/playbackclock.h"
#include "domain/playback/playbackpositionthrottler.h"
#include "domain/playback/playbackcontext.h"
#include "domain/playback/playbackcontroller.h"
#include "domain/music/playbacktimeline.h"
#include "domain/settings/playersettings.h"
#include "domain/settings/titlebarmode.h"
#include "app/isettingsstore.h"
#include "app/settingsservice.h"
#include "infrastructure/audio/threadedplaybackaudioservice.h"
#include "infrastructure/settings/qsettingsstore.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QSettings>
#include <QSemaphore>
#include <QThread>
#include <QTemporaryDir>

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <limits>

namespace {

using midi_play::music::MusicDocument;
using midi_play::music::NoteEvent;
using midi_play::music::PlaybackTimeline;
using midi_play::music::Track;
using midi_play::playback::IPlaybackAudioService;
using midi_play::playback::PlaybackBackendCapabilities;
using midi_play::playback::PlaybackClockSnapshot;
using midi_play::playback::PlaybackClockSource;
using midi_play::playback::PlaybackContext;
using midi_play::playback::PlaybackData;
using midi_play::playback::PlaybackEvent;
using midi_play::playback::PlaybackEventKind;
using midi_play::playback::PlaybackPositionThrottler;
using midi_play::playback::PlaybackSession;
using midi_play::playback::PlaybackClock;
using midi_play::playback::State;
using midi_play::settings::PlayerSettings;

[[noreturn]] void fail(const char* message)
{
    std::fprintf(stderr, "FAILED: %s\n", message);
    std::exit(EXIT_FAILURE);
}

void require(bool condition, const char* message)
{
    if (!condition) fail(message);
}

class RecordingAudioService final : public IPlaybackAudioService {
public:
    bool loadSoundFont(const QString& path, QString*) override
    {
        if (soundFontLoadDelayMs > 0) {
            QThread::msleep(static_cast<unsigned long>(soundFontLoadDelayMs));
        }
        loadedSoundFonts.push_back(path);
        return !failSoundFontLoad;
    }
    bool configureTrack(const QString&, int, int, QString*) override { return true; }
    bool addTrack(const PlaybackData&, QString*) override
    {
        ++addTrackCount;
        return true;
    }
    bool start() override
    {
        ++startCount;
        return true;
    }
    bool pause() override
    {
        ++pauseCount;
        return true;
    }
    bool stop() override { return true; }
    bool seek(qint64 positionUs) override
    {
        seekPositions.push_back(positionUs);
        return true;
    }
    bool setTransportPosition(qint64 positionUs) override
    {
        transportPositions.push_back(positionUs);
        return true;
    }
    qint64 clockPositionUs() const override
    {
        ++clockPositionQueryCount;
        return clockPositionUsValue;
    }
    std::shared_ptr<const PlaybackClockSnapshot> clockSnapshot() const override
    {
        return sharedClockSnapshot;
    }
    PlaybackBackendCapabilities capabilities() const override
    {
        ++capabilitiesQueryCount;
        return reportedCapabilities;
    }
    bool flush() override
    {
        ++flushCount;
        return true;
    }
    void submit(const PlaybackEvent& event) override { submittedEvents.push_back(event); }
    void submitOff(const PlaybackEvent& event) override { submittedEvents.push_back(event); }
    void submitBatch(const QVector<PlaybackEvent>& events, quint64 generation) override
    {
        if (blockNextSubmission.exchange(false)) {
            submissionEntered.release();
            require(resumeSubmission.tryAcquire(1, 2000), "release blocked audio worker");
        }
        submittedGenerations.push_back(generation);
        submittedEvents += events;
    }
    void setEventGeneration(quint64 generation) override { currentGeneration = generation; }
    bool prepareMetronome(QString* error) override
    {
        if (error) *error = failMetronome ? QStringLiteral("test click preparation failure") : QString();
        return reportedCapabilities.metronome && !failMetronome;
    }
    void submitMetronomeClick(bool accent, quint64 generation) override
    {
        clickAccents.push_back(accent);
        clickGenerations.push_back(generation);
    }
    void stopMetronome() override { ++clickStopCount; }

    void clearSubmissions()
    {
        submittedEvents.clear();
        submittedGenerations.clear();
    }

    int startCount = 0;
    bool failMetronome = false;
    int clickStopCount = 0;
    QVector<bool> clickAccents;
    QVector<quint64> clickGenerations;
    std::atomic<bool> blockNextSubmission {false};
    QSemaphore submissionEntered;
    QSemaphore resumeSubmission;
    int pauseCount = 0;
    int flushCount = 0;
    int addTrackCount = 0;
    int soundFontLoadDelayMs = 0;
    bool failSoundFontLoad = false;
    mutable std::atomic<int> capabilitiesQueryCount {0};
    mutable std::atomic<int> clockPositionQueryCount {0};
    qint64 clockPositionUsValue = -1;
    std::shared_ptr<PlaybackClockSnapshot> sharedClockSnapshot;
    PlaybackBackendCapabilities reportedCapabilities;
    quint64 currentGeneration = 0;
    QVector<qint64> seekPositions;
    QVector<qint64> transportPositions;
    QVector<QString> loadedSoundFonts;
    QVector<quint64> submittedGenerations;
    QVector<PlaybackEvent> submittedEvents;
};

class MemorySettingsStore final : public midi_play::app::ISettingsStore {
public:
    PlayerSettings load(QString* warning) override
    {
        ++loadCount;
        if (warning) *warning = loadWarning;
        return loadedSettings;
    }

    bool save(const PlayerSettings& settings, QString* error) override
    {
        ++saveCount;
        savedSettings = settings;
        if (error) *error = saveError;
        return saveResult;
    }

    PlayerSettings loadedSettings;
    PlayerSettings savedSettings;
    QString loadWarning;
    QString saveError;
    bool saveResult = true;
    int loadCount = 0;
    int saveCount = 0;
};

void testVisualizationRefreshRateSettingsNormalizeInts()
{
    require(midi_play::settings::normalizeVisualizationRefreshRate(30) == 30,
            "30 FPS must be accepted");
    require(midi_play::settings::normalizeVisualizationRefreshRate(60) == 60,
            "60 FPS must be accepted");
    require(midi_play::settings::normalizeVisualizationRefreshRate(120) == 120,
            "120 FPS must be accepted");
    require(midi_play::settings::normalizeVisualizationRefreshRate(1) == 1,
            "minimum custom FPS must be accepted");
    require(midi_play::settings::normalizeVisualizationRefreshRate(144) == 144,
            "custom FPS must be accepted");
    require(midi_play::settings::normalizeVisualizationRefreshRate(1001) == 60,
            "out-of-range FPS must fall back to 60");
    require(midi_play::settings::visualizationRefreshPeriod(144)
                == std::chrono::nanoseconds(6'944'444),
            "custom FPS must use a nanosecond period without millisecond truncation");
}

void testGraphicsModeSettings()
{
    require(midi_play::settings::graphicsModeFromPersistentValue(0)
                == midi_play::settings::GraphicsMode::Traditional,
            "graphics mode zero must select the traditional renderer");
    require(midi_play::settings::graphicsModeFromPersistentValue(1)
                == midi_play::settings::GraphicsMode::VulkanExperimental,
            "graphics mode one must select Vulkan experimental renderer");
    require(midi_play::settings::graphicsModeFromPersistentValue(99)
                == midi_play::settings::GraphicsMode::Traditional,
            "unknown graphics modes must fall back to traditional renderer");

    auto store = std::make_unique<MemorySettingsStore>();
    auto* rawStore = store.get();
    midi_play::app::SettingsService service(std::move(store));
    int changes = 0;
    QObject::connect(&service, &midi_play::app::SettingsService::graphicsModeChanged,
                     [&changes](midi_play::settings::GraphicsMode) { ++changes; });
    service.load();
    service.setGraphicsMode(midi_play::settings::GraphicsMode::VulkanExperimental);
    require(service.graphicsMode() == midi_play::settings::GraphicsMode::VulkanExperimental,
            "graphics mode changes must apply immediately");
    require(changes == 1 && rawStore->saveCount == 1,
            "graphics mode changes must emit and persist exactly once");
    require(rawStore->savedSettings.graphicsMode
                == midi_play::settings::GraphicsMode::VulkanExperimental,
            "persisted settings must retain Vulkan graphics mode");
}

void testTitleBarModePlatformPolicy()
{
    require(midi_play::settings::normalizeTitleBarMode(
                midi_play::settings::TitleBarMode::Native)
                == midi_play::settings::TitleBarMode::Native,
            "native title bar mode must always be valid");
#if defined(Q_OS_WIN)
    require(midi_play::settings::kDefaultTitleBarMode
                == midi_play::settings::TitleBarMode::Native,
            "Windows must default to the native title bar");
    require(midi_play::settings::normalizeTitleBarMode(
                midi_play::settings::TitleBarMode::Custom)
                == midi_play::settings::TitleBarMode::Custom,
            "Windows must allow the custom title bar");
#else
    require(midi_play::settings::kDefaultTitleBarMode
                == midi_play::settings::TitleBarMode::Native,
            "non-Windows platforms must default to the native title bar");
    require(midi_play::settings::normalizeTitleBarMode(
                midi_play::settings::TitleBarMode::Custom)
                == midi_play::settings::TitleBarMode::Native,
            "non-Windows platforms must normalize custom mode to native");
#endif
    require(midi_play::settings::titleBarModeFromPersistentValue(99)
                == midi_play::settings::TitleBarMode::Native,
            "invalid persisted title bar mode must normalize to native");
}

void testSettingsServicePersistsTitleBarMode()
{
    auto store = std::make_unique<MemorySettingsStore>();
    auto* rawStore = store.get();
    rawStore->loadedSettings.titleBarMode = midi_play::settings::TitleBarMode::Custom;
    midi_play::app::SettingsService service(std::move(store));

    int changeCount = 0;
    QObject::connect(&service, &midi_play::app::SettingsService::titleBarModeChanged,
                     [&changeCount](midi_play::settings::TitleBarMode) { ++changeCount; });
    service.load();
    const auto loadedMode = service.titleBarMode();
#if defined(Q_OS_WIN)
    require(loadedMode == midi_play::settings::TitleBarMode::Custom,
            "Windows must preserve the custom title bar mode");
#else
    require(loadedMode == midi_play::settings::TitleBarMode::Native,
            "non-Windows platforms must normalize custom mode to native");
#endif

    service.setTitleBarMode(midi_play::settings::TitleBarMode::Native);
    require(service.titleBarMode() == midi_play::settings::TitleBarMode::Native,
            "title bar mode changes must apply immediately");
#if defined(Q_OS_WIN)
    require(changeCount == 1, "effective title bar mode changes must emit once");
    require(rawStore->saveCount == 1, "title bar mode changes must be persisted");
#else
    require(changeCount == 0, "unsupported title bar mode changes must be ignored");
    require(rawStore->saveCount == 0, "unsupported title bar mode changes must not be persisted");
#endif
}

void testSettingsServicePersistsAndResetsSoundFont()
{
    auto store = std::make_unique<MemorySettingsStore>();
    auto* rawStore = store.get();
    const QString defaultPath = QDir::cleanPath(
        QDir::temp().absoluteFilePath(QStringLiteral("midi-play/default/midisound.sf2")));
    const QString customPath = QDir::cleanPath(
        QDir::temp().absoluteFilePath(QStringLiteral("midi-play/custom/orchestra.sf2")));
    midi_play::app::SettingsService service(std::move(store), defaultPath);
    service.load();

    require(service.usesDefaultSoundFont(),
            "missing SoundFont override must select the bundled default");
    require(service.soundFontPath() == defaultPath,
            "effective default SoundFont path must be exposed");

    int changeCount = 0;
    QString changedPath;
    bool changedToDefault = false;
    QObject::connect(&service, &midi_play::app::SettingsService::soundFontPathChanged,
                     [&](const QString& path, bool usesDefault) {
                         ++changeCount;
                         changedPath = path;
                         changedToDefault = usesDefault;
                     });

    service.setSoundFontPath(customPath);
    require(!service.usesDefaultSoundFont(),
            "custom SoundFont must replace the effective default");
    require(service.soundFontPath() == customPath,
            "custom SoundFont path must be normalized and exposed");
    require(rawStore->saveCount == 1
                && rawStore->savedSettings.soundFontPathOverride == customPath,
            "custom SoundFont override must be persisted");
    require(changeCount == 1 && changedPath == customPath && !changedToDefault,
            "custom SoundFont change must publish its effective state");

    service.resetSoundFontPath();
    require(service.usesDefaultSoundFont() && service.soundFontPath() == defaultPath,
            "reset must restore the bundled default SoundFont");
    require(rawStore->saveCount == 2
                && rawStore->savedSettings.soundFontPathOverride.isEmpty(),
            "reset must persist an empty override rather than the installed path");
    require(changeCount == 2 && changedPath == defaultPath && changedToDefault,
            "reset must publish the effective default SoundFont");

    service.setSoundFontPath(defaultPath);
    require(rawStore->saveCount == 2 && changeCount == 2,
            "selecting the bundled SoundFont must remain equivalent to reset");
}

void testSettingsServicePersistsOnlyEffectiveChanges()
{
    auto store = std::make_unique<MemorySettingsStore>();
    auto* rawStore = store.get();
    rawStore->loadedSettings.visualizationRefreshRate = 120;
    midi_play::app::SettingsService service(std::move(store));

    int changeCount = 0;
    int saveFailureCount = 0;
    QObject::connect(&service, &midi_play::app::SettingsService::visualizationRefreshRateChanged,
                     [&changeCount](int) { ++changeCount; });
    QObject::connect(&service, &midi_play::app::SettingsService::settingsSaveFailed,
                     [&saveFailureCount](const QString&) { ++saveFailureCount; });

    service.load();
    require(service.visualizationRefreshRate() == 120,
            "settings service must expose the loaded refresh rate");
    require(rawStore->loadCount == 1, "settings service must load exactly once");

    service.setVisualizationRefreshRate(120);
    require(changeCount == 0, "same refresh rate must not emit a change");
    require(rawStore->saveCount == 0, "same refresh rate must not be saved again");

    service.setVisualizationRefreshRate(30);
    require(service.visualizationRefreshRate() == 30,
            "settings service must apply a supported refresh rate");
    require(changeCount == 1, "effective refresh rate changes must emit once");
    require(rawStore->saveCount == 1, "effective refresh rate changes must be saved once");
    require(rawStore->savedSettings.visualizationRefreshRate == 30,
            "settings service must save the normalized refresh rate");

    rawStore->saveResult = false;
    rawStore->saveError = QStringLiteral("save failed");
    service.setVisualizationRefreshRate(1001);
    require(service.visualizationRefreshRate() == 60,
            "invalid runtime refresh rate must fall back to 60");
    require(saveFailureCount == 1, "save failures must be reported");
}

void testQSettingsStorePersistsUserRefreshRate()
{
    QTemporaryDir directory;
    require(directory.isValid(), "temporary settings directory must be available");

    const QString settingsPath = QDir(directory.path()).filePath(QStringLiteral("settings.ini"));
    midi_play::infrastructure::settings::QSettingsStore store(settingsPath);

    QString warning;
    PlayerSettings loaded = store.load(&warning);
    require(loaded.visualizationRefreshRate == 60,
            "missing settings file must load the default refresh rate");

    PlayerSettings saved;
    saved.visualizationRefreshRate = 120;
    saved.titleBarMode = midi_play::settings::TitleBarMode::Custom;
    saved.soundFontPathOverride = QStringLiteral("C:/SoundFonts/custom.sf2");
    QString error;
    require(store.save(saved, &error), "settings store must save a valid refresh rate");
    require(error.isEmpty(), "successful settings save must not report an error");

    loaded = store.load(&warning);
    require(loaded.visualizationRefreshRate == 120,
            "settings store must reload the persisted refresh rate");
    require(loaded.soundFontPathOverride == saved.soundFontPathOverride,
            "settings store must reload the custom SoundFont override");
#if defined(Q_OS_WIN)
    require(loaded.titleBarMode == midi_play::settings::TitleBarMode::Custom,
            "Windows settings store must reload the custom title bar mode");
#else
    require(loaded.titleBarMode == midi_play::settings::TitleBarMode::Native,
            "non-Windows settings store must normalize custom title bar mode");
#endif

    QSettings file(settingsPath, QSettings::IniFormat);
    file.setValue(QStringLiteral("General/visualizationRefreshRate"), 144);
    file.sync();

    loaded = store.load(&warning);
    require(loaded.visualizationRefreshRate == 144,
            "custom persisted refresh rate must reload unchanged");
    require(warning.isEmpty(), "valid custom refresh rate must not report a warning");

    file.setValue(QStringLiteral("General/visualizationRefreshRate"), 1001);
    file.setValue(QStringLiteral("General/titleBarMode"), 99);
    file.sync();
    loaded = store.load(&warning);
    require(loaded.visualizationRefreshRate == 60,
            "out-of-range persisted refresh rate must fall back to 60");
    require(!warning.isEmpty(), "invalid persisted refresh rate should report a warning");
    require(loaded.titleBarMode == midi_play::settings::TitleBarMode::Native,
            "invalid persisted title bar mode must fall back to native");

    loaded.soundFontPathOverride.clear();
    require(store.save(loaded, &error),
            "settings store must save a reset SoundFont configuration");
    QSettings resetFile(settingsPath, QSettings::IniFormat);
    require(!resetFile.contains(QStringLiteral("Audio/soundFontPath")),
            "reset must remove the custom SoundFont key from the settings file");
}

void testPlaybackTimelineCachesRepeatExpansion()
{
    auto document = std::make_shared<MusicDocument>();
    document->setDuration(1'920);
    document->tempos().push_back({0, 120.0, 0});
    document->tempos().push_back({960, 60.0, 1});

    midi_play::music::Measure first;
    first.number = 1;
    first.start = 0;
    first.duration = 960;
    first.repeatStart = true;
    midi_play::music::Measure second;
    second.number = 2;
    second.start = 960;
    second.duration = 960;
    second.repeatEnd = true;
    second.repeatCount = 2;
    document->measures() = {first, second};

    PlaybackTimeline timeline(document);
    require(timeline.segments().size() == 4,
            "timeline must expand a two-measure repeat exactly once");
    require(timeline.durationTicks() == 3'840,
            "timeline must cache playback-order tick duration");
    require(timeline.durationUs() == 6'000'000,
            "timeline must accumulate tempo-aware repeated duration");
    require(timeline.outputTickToMicroseconds(960) == 1'000'000,
            "timeline must resolve the first segment boundary");
    require(timeline.outputTickToMicroseconds(1'920) == 3'000'000,
            "timeline must resolve the repeated section boundary");
    require(timeline.outputTickToMicroseconds(2'880) == 4'000'000,
            "timeline must resolve the second-pass tempo boundary");
    require(timeline.outputTickToMicroseconds(3'840) == 6'000'000,
            "timeline must clamp the playback endpoint to cached duration");
}

void testThreadedAudioCapabilitiesAreImmutableSnapshot()
{
    auto audio = std::make_unique<RecordingAudioService>();
    auto* recording = audio.get();
    recording->reportedCapabilities = {
        PlaybackClockSource::SoftwareMonotonic, false, false
    };

    midi_play::audio::ThreadedPlaybackAudioService threaded(std::move(audio));
    require(recording->capabilitiesQueryCount == 1,
            "threaded audio service must capture capabilities once");
    for (int i = 0; i < 100; ++i) {
        require(threaded.clockSource() == PlaybackClockSource::SoftwareMonotonic,
                "cached clock source must remain stable");
        require(!threaded.supportsTimedEvents(),
                "cached timed-event capability must remain stable");
        require(!threaded.supportsPerNoteExpression(),
                "cached expression capability must remain stable");
        require(!threaded.capabilities().usesAudioClock(),
                "capability snapshot must remain locally readable");
    }
    require(recording->capabilitiesQueryCount == 1,
            "capability reads must not cross into the audio worker");
}

void testPlaybackContextProjectsDynamicsThroughRepeats()
{
    auto document = std::make_shared<MusicDocument>();
    document->setDuration(1'920);
    document->tempos().push_back({0, 120.0, 0});

    midi_play::music::Measure first;
    first.number = 1;
    first.start = 0;
    first.duration = 960;
    first.repeatStart = true;
    midi_play::music::Measure second;
    second.number = 2;
    second.start = 960;
    second.duration = 960;
    second.repeatEnd = true;
    second.repeatCount = 2;
    document->measures() = {first, second};

    Track dynamicsTrack;
    dynamicsTrack.id = QStringLiteral("repeat-dynamics");
    dynamicsTrack.dynamics = {{0, 40}, {960, 100}};
    dynamicsTrack.hairpins.push_back({0, 960, true});
    document->tracks().push_back(dynamicsTrack);

    Track spanningHairpinTrack;
    spanningHairpinTrack.id = QStringLiteral("spanning-hairpin");
    spanningHairpinTrack.dynamics = {{0, 40}};
    spanningHairpinTrack.hairpins.push_back({480, 1'440, true});
    document->tracks().push_back(spanningHairpinTrack);

    auto timeline = std::make_shared<PlaybackTimeline>(document);
    PlaybackContext context(document, timeline);

    require(context.velocityAt(dynamicsTrack.id, 1'500'000, 90) == 100,
            "first repeat pass must apply the second-measure dynamic");
    require(context.velocityAt(dynamicsTrack.id, 2'000'000, 90) == 40,
            "second repeat pass must restore the first-measure dynamic");
    require(context.velocityAt(dynamicsTrack.id, 2'500'000, 90) == 58,
            "second repeat pass must replay its crescendo");

    require(context.velocityAt(spanningHairpinTrack.id, 750'000, 90) == 49,
            "hairpin progress before a segment boundary must be preserved");
    require(context.velocityAt(spanningHairpinTrack.id, 1'000'000, 90) == 58,
            "hairpin must be applied exactly once at a segment boundary");
    require(context.velocityAt(spanningHairpinTrack.id, 1'250'000, 90) == 67,
            "hairpin progress after a segment boundary must remain continuous");
    require(context.velocityAt(spanningHairpinTrack.id, 2'750'000, 90) == 49,
            "spanning hairpin must restart on the repeated pass");
}

void testThreadedAudioClockUsesAtomicSnapshot()
{
    auto audio = std::make_unique<RecordingAudioService>();
    auto* recording = audio.get();
    auto clockSnapshot = std::make_shared<PlaybackClockSnapshot>();
    recording->reportedCapabilities.clockSource = PlaybackClockSource::AudioDevice;
    recording->sharedClockSnapshot = clockSnapshot;
    clockSnapshot->publish(750'000);

    midi_play::audio::ThreadedPlaybackAudioService threaded(std::move(audio));
    require(threaded.clockSource() == PlaybackClockSource::AudioDevice,
            "threaded service must retain a safely published audio clock");
    for (int i = 0; i < 100; ++i) {
        require(threaded.clockPositionUs() == 750'000,
                "threaded clock reads must use the atomic snapshot");
    }
    require(recording->clockPositionQueryCount == 0,
            "threaded clock reads must never call the worker service directly");

    clockSnapshot->publish(900'000);
    require(threaded.clockPositionUs() == 900'000,
            "threaded clock snapshot must expose newly published positions");
    require(recording->clockPositionQueryCount == 0,
            "updated snapshot reads must remain independent of the worker service");
}

void testThreadedAudioClockWithoutSnapshotFallsBackSafely()
{
    auto audio = std::make_unique<RecordingAudioService>();
    auto* recording = audio.get();
    recording->reportedCapabilities.clockSource = PlaybackClockSource::AudioDevice;

    midi_play::audio::ThreadedPlaybackAudioService threaded(std::move(audio));
    require(threaded.clockSource() == PlaybackClockSource::SoftwareMonotonic,
            "missing atomic device snapshot must fall back to the software clock");
    require(threaded.clockPositionUs() < 0,
            "missing atomic device snapshot must not expose an unsafe clock");
    require(recording->clockPositionQueryCount == 0,
            "fallback must not call the worker service across threads");
}

std::shared_ptr<const MusicDocument> testDocument()
{
    auto document = std::make_shared<MusicDocument>();
    document->tempos().push_back({0, 120.0, 0});
    document->setDuration(1'920); // Two seconds at 120 BPM.

    Track track;
    track.id = QStringLiteral("transport-test");
    NoteEvent note;
    note.noteId = 1;
    note.start = 0;
    note.duration = 1'920;
    note.pitch = 60;
    track.notes.push_back(note);
    document->tracks().push_back(track);
    document->rebuildMeasureGrid();
    return document;
}

bool waitUntil(const std::function<bool()>& predicate, int timeoutMs = 500)
{
    QElapsedTimer timeout;
    timeout.start();
    while (!predicate() && timeout.elapsed() < timeoutMs) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
        QThread::msleep(1);
    }
    return predicate();
}

void testAudioClockCapabilityEnablesClockSampling()
{
    auto audio = std::make_unique<RecordingAudioService>();
    auto* recording = audio.get();
    recording->reportedCapabilities.clockSource = PlaybackClockSource::AudioDevice;
    recording->clockPositionUsValue = 750'000;
    PlaybackSession session(testDocument(), std::move(audio));

    session.play();
    require(waitUntil([&] { return recording->clockPositionQueryCount > 0; }),
            "audio-clock backend must be sampled while playing");
    require(session.positionMicroseconds() == recording->clockPositionUsValue,
            "audio-clock backend must drive the playhead position");
    session.pause();
    require(recording->clockPositionQueryCount > 1,
            "pause must capture the final audio-clock position");
}

void processEventsFor(int durationMs)
{
    QElapsedTimer duration;
    duration.start();
    while (duration.elapsed() < durationMs) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
        QThread::msleep(1);
    }
}

bool containsNoteOn(const QVector<PlaybackEvent>& events)
{
    for (const auto& event : events) {
        if (event.kind == PlaybackEventKind::NoteOn) return true;
    }
    return false;
}

void testSeekPreservesTransportIntent()
{
    auto audio = std::make_unique<RecordingAudioService>();
    auto* recording = audio.get();
    PlaybackSession session(testDocument(), std::move(audio));

    session.play();
    require(session.state() == State::Playing, "play must enter Playing state");
    require(waitUntil([&] { return session.positionMicroseconds() > 0; }),
            "playing session must advance before seek");
    require(recording->capabilitiesQueryCount == 1,
            "playback session must capture backend capabilities once");
    require(recording->clockPositionQueryCount == 0,
            "software-clock playback must not query the audio clock");

    constexpr qint64 playingSeekTargetUs = 1'000'000;
    recording->clearSubmissions();
    session.seek(playingSeekTargetUs);
    require(session.state() == State::Playing, "playing seek must preserve Playing state");
    require(session.positionMicroseconds() == playingSeekTargetUs,
            "playing seek must publish the release position immediately");
    require(recording->startCount == 2, "playing seek must restart audio scheduling");
    require(containsNoteOn(recording->submittedEvents),
            "playing seek must restore a note spanning the target position");
    require(waitUntil([&] { return session.positionMicroseconds() > playingSeekTargetUs; }),
            "playing seek must continue advancing without a pause/play cycle");

    session.pause();
    require(session.state() == State::Paused, "pause must enter Paused state");
    require(recording->clockPositionQueryCount == 0,
            "software-clock pause must not query the audio clock");
    constexpr qint64 pausedSeekTargetUs = 500'000;
    recording->clearSubmissions();
    session.seek(pausedSeekTargetUs);
    require(session.state() == State::Paused, "paused seek must preserve Paused state");
    require(session.positionMicroseconds() == pausedSeekTargetUs,
            "paused seek must move to the release position");
    require(recording->submittedEvents.isEmpty(),
            "paused seek must not submit controller or note events");
    processEventsFor(30);
    require(session.positionMicroseconds() == pausedSeekTargetUs,
            "paused seek position must remain stationary");

    session.play();
    require(session.state() == State::Playing, "play after paused seek must resume playback");
    require(containsNoteOn(recording->submittedEvents),
            "play after paused seek must restore a spanning note");
    require(waitUntil([&] { return session.positionMicroseconds() > pausedSeekTargetUs; }),
            "play after paused seek must advance from the selected position");
}

void testSoundFontChangeRestoresPlayingAudioState()
{
    auto audio = std::make_unique<RecordingAudioService>();
    auto* recording = audio.get();
    PlaybackSession session(testDocument(), std::move(audio));

    session.play();
    require(waitUntil([&] { return session.positionMicroseconds() > 0; }),
            "playing session must advance before changing SoundFont");
    recording->clearSubmissions();
    const int transportUpdateCount = recording->transportPositions.size();

    QString error;
    require(session.loadSoundFont(QStringLiteral("orchestra.sf2"), &error),
            "a valid SoundFont change must succeed");
    require(error.isEmpty(), "successful SoundFont changes must not report an error");
    require(recording->loadedSoundFonts.size() == 1
                && recording->loadedSoundFonts.front() == QStringLiteral("orchestra.sf2"),
            "SoundFont change must reach the audio service");
    require(recording->addTrackCount == 1,
            "SoundFont change must configure each playback track");
    require(recording->transportPositions.size() == transportUpdateCount + 1,
            "live SoundFont change must restore the current transport position");
    require(containsNoteOn(recording->submittedEvents),
            "live SoundFont change must restore notes spanning the playhead");
    require(session.state() == State::Playing,
            "live SoundFont change must preserve the playing state");
    session.pause();
}

void testSoundFontChangeFreezesAndRestoresTransport()
{
    auto audio = std::make_unique<RecordingAudioService>();
    auto* recording = audio.get();
    recording->soundFontLoadDelayMs = 40;
    PlaybackSession session(testDocument(), std::move(audio));

    session.play();
    require(waitUntil([&] { return session.positionMicroseconds() > 20'000; }),
            "playing session must advance before timing a SoundFont change");
    const qint64 beforeChangeUs = session.positionMicroseconds();
    QString error;
    require(session.loadSoundFont(QStringLiteral("delayed.sf2"), &error),
            "delayed SoundFont changes must succeed");
    processEventsFor(5);
    const qint64 afterChangeUs = session.positionMicroseconds();
    require(afterChangeUs - beforeChangeUs < 25'000,
            "SoundFont loading time must not advance the transport clock");
    require(recording->pauseCount == 1 && recording->flushCount > 0,
            "live SoundFont changes must quiesce previous audio before switching");
    session.pause();
}

void testFailedSoundFontChangeRestoresTransport()
{
    auto audio = std::make_unique<RecordingAudioService>();
    auto* recording = audio.get();
    PlaybackSession session(testDocument(), std::move(audio));

    session.play();
    require(waitUntil([&] { return session.positionMicroseconds() > 20'000; }),
            "playing session must advance before a failed SoundFont change");
    recording->failSoundFontLoad = true;
    QString error;
    require(!session.loadSoundFont(QStringLiteral("broken.sf2"), &error),
            "failed SoundFont changes must report failure");
    require(session.state() == State::Playing,
            "failed SoundFont changes must preserve the transport state");
    const qint64 restoredPositionUs = session.positionMicroseconds();
    require(waitUntil([&] { return session.positionMicroseconds() > restoredPositionUs; }),
            "failed SoundFont changes must resume transport progress");
    session.pause();
}

void testPositionThrottlerUsesLatestSample()
{
    PlaybackPositionThrottler throttler;
    PlaybackPositionThrottler::Snapshot snapshot;
    require(!throttler.takeLatest(snapshot), "empty position throttler must not publish");

    throttler.publish(10, 1000);
    throttler.publish(20, 2000);
    require(throttler.takeLatest(snapshot), "published position must be consumable");
    require(snapshot.positionUs == 20 && snapshot.durationUs == 2000,
            "position throttler must coalesce to the latest sample");
    require(!throttler.takeLatest(snapshot), "consumed sample must not be repeated");

    throttler.publish(30, 3000);
    throttler.reset();
    require(!throttler.takeLatest(snapshot), "reset must discard pending samples");
}

void testPlaybackClockRateChangeIsContinuous()
{
    PlaybackClock clock;
    clock.start(100'000);
    QThread::msleep(20);
    QElapsedTimer changeTime;
    changeTime.start();
    const qint64 before = clock.positionUs();
    clock.setRate(2.0);
    const qint64 rebased = clock.positionUs();
    require(rebased >= before && rebased - before <= changeTime.nsecsElapsed() / 500 + 2'000,
            "changing playback rate must keep the clock position continuous");
    for (const double rate : {2.0, 0.2, 1.25}) {
        clock.setRate(rate);
        QElapsedTimer wallTime;
        wallTime.start();
        const qint64 start = clock.positionUs();
        QThread::msleep(30);
        const qint64 delta = clock.positionUs() - start;
        const qint64 expected = static_cast<qint64>(wallTime.nsecsElapsed() / 1000.0 * rate);
        require(std::llabs(delta - expected) < 5'000,
                "clock progress must follow the selected rate and actual elapsed wall time");
    }

    clock.pause(clock.positionUs());
    const qint64 paused = clock.positionUs();
    clock.setRate(0.5);
    QThread::msleep(15);
    require(clock.positionUs() == paused,
            "changing rate while paused must not advance the clock");
    clock.setRate(std::numeric_limits<double>::quiet_NaN());
    clock.setRate(std::numeric_limits<double>::infinity());
    clock.setRate(-1.0);
    require(clock.rate() == 0.5, "invalid floating-point rates must leave the clock valid");
    clock.start(std::numeric_limits<qint64>::max() - 100);
    QThread::msleep(2);
    require(clock.positionUs() == std::numeric_limits<qint64>::max(),
            "scaled clock arithmetic must saturate rather than overflow");
}

void testPlaybackSessionRateChangePreservesAudioState()
{
    auto audio = std::make_unique<RecordingAudioService>();
    auto* recording = audio.get();
    PlaybackSession session(testDocument(), std::move(audio));
    session.play();
    require(waitUntil([&] { return session.positionMicroseconds() > 20'000; }),
            "session must advance before changing playback rate");
    const int starts = recording->startCount;
    const int pauses = recording->pauseCount;
    const int flushes = recording->flushCount;
    const qint64 before = session.positionMicroseconds();
    session.setPlaybackRatePercent(200);
    require(session.playbackRatePercent() == 200,
            "session must expose the normalized playback rate");
    require(recording->startCount == starts && recording->pauseCount == pauses
                && recording->flushCount == flushes,
            "changing playback rate must not restart or flush the audio service");
    require(std::llabs(session.positionMicroseconds() - before) < 5'000,
            "session rate change must keep the transport position continuous");
    session.setPlaybackRatePercent(1);
    require(session.playbackRatePercent() == 20,
            "session must clamp playback rate to the 20 percent minimum");
    session.pause();
    const qint64 paused = session.positionMicroseconds();
    session.setPlaybackRatePercent(250);
    processEventsFor(20);
    require(session.playbackRatePercent() == 200 && session.positionMicroseconds() == paused,
            "upper bound normalization while paused must preserve the position");
    session.seek(350'000);
    require(session.positionMicroseconds() == 350'000 && session.state() == State::Paused,
            "seek must continue to accept source music time at non-default speeds");
    session.play();
    require(waitUntil([&] { return session.positionMicroseconds() > 360'000; }),
            "play must resume a rate-adjusted paused seek");
    require(session.loadSoundFont(QStringLiteral("test.sf2"), nullptr)
                && session.playbackRatePercent() == 200,
            "SoundFont replacement must retain the selected rate");
    session.stop();
    require(session.playbackRatePercent() == 200 && session.positionMicroseconds() == 0,
            "stop must reset position while retaining speed");
    session.play();
    session.seek(session.durationMicroseconds() - 10'000);
    require(waitUntil([&] { return session.state() == State::Stopped; }),
            "a rate-adjusted playhead must still stop at the end of the song");
}

void testRateChangesPreserveEventOrderAndPitch()
{
    auto document = std::make_shared<MusicDocument>();
    document->tempos().push_back({0, 120.0, 0});
    document->tempos().push_back({96, 60.0, 1});
    document->setDuration(960);
    Track track;
    track.id = QStringLiteral("rate-test");
    NoteEvent note;
    note.noteId = 1;
    note.start = 48; // 50 ms of music time.
    note.duration = 144; // Ends at 300 ms, spanning the tempo change.
    note.pitch = 64;
    track.notes.push_back(note);
    track.controlChanges.push_back({72, 0, 64, 127, 1});
    track.controlChanges.push_back({240, 0, 64, 0, 2});
    document->tracks().push_back(track);
    document->rebuildMeasureGrid();
    auto audio = std::make_unique<RecordingAudioService>();
    auto* recording = audio.get();
    PlaybackSession session(document, std::move(audio));
    session.setPlaybackRatePercent(200);
    session.play();
    require(waitUntil([&] { return containsNoteOn(recording->submittedEvents); }),
            "200 percent playback must dispatch note-on");
    const quint64 generation = recording->currentGeneration;
    session.setPlaybackRatePercent(20);
    processEventsFor(30);
    session.setPlaybackRatePercent(125);
    require(waitUntil([&] { return session.positionMicroseconds() >= 450'000; }, 2000),
            "live rate changes must continue beyond note release and sustain release");

    QVector<PlaybackEvent> relevant;
    for (const auto& event : recording->submittedEvents) {
        if (event.kind == PlaybackEventKind::NoteOn || event.kind == PlaybackEventKind::NoteOff
            || (event.kind == PlaybackEventKind::ControlChange && event.controller == 64)) {
            relevant.push_back(event);
        }
    }
    require(relevant.size() == 4 && relevant[0].kind == PlaybackEventKind::NoteOn
                && relevant[1].kind == PlaybackEventKind::ControlChange
                && relevant[2].kind == PlaybackEventKind::NoteOff
                && relevant[3].kind == PlaybackEventKind::ControlChange,
            "changing speed must neither duplicate nor lose notes and sustain events");
    require(relevant[0].pitch == 64 && relevant[2].pitch == 64
                && relevant[0].timestampUs == 50'000 && relevant[2].timestampUs == 300'000,
            "speed changes must preserve pitch and tempo-derived source timestamps");
    require(recording->currentGeneration == generation && recording->flushCount == 0,
            "live speed changes must keep the audio generation and sounding notes intact");
    session.pause();
}

void testControllerRateChangesAreQueuedAndInherited()
{
    midi_play::playback::PlaybackController controller;
    controller.setPlaybackRatePercent(150);
    require(controller.setDocument(testDocument(), std::make_unique<RecordingAudioService>(), nullptr),
            "controller must create a session with a preselected speed");
    auto* session = controller.session();
    int appliedRate = 0;
    QMetaObject::invokeMethod(session, [&] { appliedRate = session->playbackRatePercent(); },
                              Qt::BlockingQueuedConnection);
    require(appliedRate == 150, "new sessions must inherit the preselected speed");

    QSemaphore entered;
    QSemaphore resume;
    QMetaObject::invokeMethod(session, [&] {
        entered.release();
        resume.tryAcquire(1, 1000);
    }, Qt::QueuedConnection);
    require(entered.tryAcquire(1, 1000), "test must occupy the playback worker");
    QElapsedTimer elapsed;
    elapsed.start();
    controller.setPlaybackRatePercent(20);
    controller.setPlaybackRatePercent(75);
    controller.setPlaybackRatePercent(200);
    require(elapsed.elapsed() < 200, "rate edits must not block behind a busy playback worker");
    resume.release();
    QMetaObject::invokeMethod(session, [&] { appliedRate = session->playbackRatePercent(); },
                              Qt::BlockingQueuedConnection);
    require(appliedRate == 200, "queued edits must finish with the latest selected rate");
    require(controller.setDocument(testDocument(), std::make_unique<RecordingAudioService>(), nullptr),
            "controller must support replacing a rate-adjusted session");
    session = controller.session();
    QMetaObject::invokeMethod(session, [&] { appliedRate = session->playbackRatePercent(); },
                              Qt::BlockingQueuedConnection);
    require(appliedRate == 200, "song replacement must retain the current speed");
}

void testUnsupportedBackendCannotSilentlyIgnoreRate()
{
    for (const auto capabilities : {
             PlaybackBackendCapabilities {PlaybackClockSource::AudioDevice, false, false},
             PlaybackBackendCapabilities {PlaybackClockSource::SoftwareMonotonic, true, false}}) {
        auto audio = std::make_unique<RecordingAudioService>();
        audio->reportedCapabilities = capabilities;
        PlaybackSession session(testDocument(), std::move(audio));
        require(!session.setPlaybackRatePercent(200) && session.playbackRatePercent() == 100,
                "device-clock or timed backends need a rate contract before accepting speed changes");
    }
}


void testMetronomeTransport()
{
    auto audio = std::make_unique<RecordingAudioService>();
    auto* recording = audio.get();
    recording->reportedCapabilities.metronome = true;
    PlaybackSession session(testDocument(), std::move(audio));
    require(!session.metronomeEnabled(), "metronome defaults off");
    require(session.loadSoundFont(QStringLiteral("test.sf2"), nullptr) && session.supportsMetronome(),
            "prepare independent clicks with soundfont load");
    session.setPlaybackRatePercent(200);
    session.seek(250000);
    session.play();
    session.setMetronomeEnabled(true);
    const int flushes = recording->flushCount;
    processEventsFor(30);
    require(recording->clickAccents.isEmpty(), "enabling mid-beat must not click immediately");
    require(waitUntil([&] { return !recording->clickAccents.isEmpty(); }), "next beat sounds");
    require(!recording->clickAccents.front()
            && recording->clickGenerations.back() == recording->currentGeneration,
            "weak beat uses the song generation");
    session.setMetronomeEnabled(false);
    const auto count = recording->clickAccents.size();
    processEventsFor(30);
    require(recording->clickAccents.size() == count && recording->flushCount == flushes,
            "toggle off must leave song MIDI and its voices intact");
    session.setMetronomeEnabled(true);
    session.stop();
    require(session.metronomeEnabled(), "stop retains metronome preference");
    session.play();
    require(waitUntil([&] { return recording->clickAccents.size() > count; }), "replay resets click cursor");
    require(recording->clickAccents.back(), "replay begins with the first downbeat");
    session.pause();
    const auto pausedCount = recording->clickAccents.size();
    processEventsFor(30);
    require(recording->clickAccents.size() == pausedCount, "paused transport is silent");
    session.play();
    processEventsFor(20);
    require(recording->clickAccents.size() == pausedCount, "resume must not duplicate consumed beat");
    session.seek(1500000);
    require(waitUntil([&] { return recording->clickAccents.size() > pausedCount; }), "seek to beat clicks once");
    require(!recording->clickAccents.back(), "seek restores correct accent");
    const auto beforeLoad = recording->clickAccents.size();
    require(session.loadSoundFont(QStringLiteral("replacement.sf2"), nullptr), "replace user font while clicking");
    processEventsFor(20);
    require(recording->clickAccents.size() == beforeLoad, "font replacement must not replay current beat");
    session.stop();
    session.setPlaybackRatePercent(20);
    session.seek(499000);
    session.play();
    require(waitUntil([&] { return recording->clickAccents.size() > beforeLoad; }), "20 percent follows source beat position");
    session.seek(session.durationMicroseconds());
    processEventsFor(10);
    require(session.state() == State::Stopped && session.metronomeEnabled(),
            "end-of-song stops sound and retains preference");
}

void testMetronomeFailureAndPreference()
{
    auto audio = std::make_unique<RecordingAudioService>();
    audio->reportedCapabilities.metronome = true;
    audio->failMetronome = true;
    PlaybackSession session(testDocument(), std::move(audio));
    session.setMetronomeEnabled(true);
    require(session.loadSoundFont(QStringLiteral("test.sf2"), nullptr),
            "optional click preparation failure must not reject the song font");
    require(!session.supportsMetronome() && !session.metronomeUnavailableReason().isEmpty(),
            "unavailable metronome has an explicit reason");
    session.play();
    require(session.state() == State::Playing && session.metronomeEnabled(),
            "song playback and desired preference survive metronome failure");
    session.stop();

    midi_play::playback::PlaybackController controller;
    controller.setMetronomeEnabled(true);
    require(controller.setDocument(testDocument(), std::make_unique<RecordingAudioService>(), nullptr),
            "create unsupported session without losing desired state");
    bool enabled = false;
    auto* active = controller.session();
    QMetaObject::invokeMethod(active, [&] { enabled = active->metronomeEnabled(); }, Qt::BlockingQueuedConnection);
    require(enabled && controller.metronomeEnabled(), "controller and session retain preference when unavailable");
    auto next = std::make_unique<RecordingAudioService>();
    next->reportedCapabilities.metronome = true;
    require(controller.setDocument(testDocument(), std::move(next), nullptr)
            && controller.loadSoundFont(QStringLiteral("test.sf2"), nullptr)
            && controller.supportsMetronome() && controller.metronomeEnabled(),
            "next supported song restores enabled metronome");
    // The preceding blocking load posted an availability signal which has not
    // reached the GUI queue yet. Replace that session before delivering it.
    require(controller.setDocument(testDocument(), std::make_unique<RecordingAudioService>(), nullptr),
            "replace supported session before its queued notification arrives");
    processEventsFor(10);
    require(!controller.supportsMetronome(), "old session availability cannot overwrite a replacement song");
}

void testThreadedMetronomeCancellation()
{
    auto audio = std::make_unique<RecordingAudioService>();
    auto* recording = audio.get();
    recording->reportedCapabilities.metronome = true;
    midi_play::audio::ThreadedPlaybackAudioService threaded(std::move(audio));
    threaded.setEventGeneration(7);
    require(threaded.prepareMetronome(nullptr), "prepare on audio worker");
    PlaybackEvent event;
    event.kind = PlaybackEventKind::NoteOn;
    event.pitch = 60;
    recording->blockNextSubmission.store(true);
    threaded.submitBatch({event}, 7);
    require(recording->submissionEntered.tryAcquire(1, 1000), "occupy audio worker");
    threaded.submitMetronomeClick(true, 7);
    threaded.submitBatch({event}, 7);
    threaded.stopMetronome();
    recording->resumeSubmission.release();
    threaded.start(); // Worker barrier: inspect only after all queued work.
    require(recording->clickAccents.isEmpty() && recording->clickStopCount == 1
            && recording->submittedEvents.size() == 2, "cancel queued clicks without cancelling song MIDI");

    recording->blockNextSubmission.store(true);
    threaded.submitBatch({event}, 7);
    require(recording->submissionEntered.tryAcquire(1, 1000), "occupy worker for deadline test");
    threaded.submitMetronomeClick(false, 7);
    QThread::msleep(80);
    recording->resumeSubmission.release();
    threaded.start();
    require(recording->clickAccents.isEmpty(), "discard clicks delayed inside audio queue");
    threaded.submitMetronomeClick(false, 7);
    threaded.start();
    require(recording->clickAccents.size() == 1 && !recording->clickAccents.front(),
            "fresh clicks accepted with correct transport generation");
    threaded.setEventGeneration(8);
    threaded.submitMetronomeClick(true, 7);
    threaded.start();
    require(recording->clickAccents.size() == 1, "old transport generation is rejected");
}

} // namespace

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    testVisualizationRefreshRateSettingsNormalizeInts();
    testGraphicsModeSettings();
    testTitleBarModePlatformPolicy();
    testSettingsServicePersistsTitleBarMode();
    testSettingsServicePersistsAndResetsSoundFont();
    testSettingsServicePersistsOnlyEffectiveChanges();
    testQSettingsStorePersistsUserRefreshRate();
    testPlaybackTimelineCachesRepeatExpansion();
    testThreadedAudioCapabilitiesAreImmutableSnapshot();
    testPlaybackContextProjectsDynamicsThroughRepeats();
    testThreadedAudioClockUsesAtomicSnapshot();
    testThreadedAudioClockWithoutSnapshotFallsBackSafely();
    testAudioClockCapabilityEnablesClockSampling();
    testPositionThrottlerUsesLatestSample();
    testPlaybackClockRateChangeIsContinuous();
    testPlaybackSessionRateChangePreservesAudioState();
    testRateChangesPreserveEventOrderAndPitch();
    testControllerRateChangesAreQueuedAndInherited();
    testUnsupportedBackendCannotSilentlyIgnoreRate();
    testMetronomeTransport();
    testMetronomeFailureAndPreference();
    testThreadedMetronomeCancellation();
    testSeekPreservesTransportIntent();
    testSoundFontChangeRestoresPlayingAudioState();
    testSoundFontChangeFreezesAndRestoresTransport();
    testFailedSoundFontChangeRestoresTransport();
    std::fprintf(stdout, "playback session tests passed\n");
    return EXIT_SUCCESS;
}
