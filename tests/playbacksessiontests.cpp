#include "domain/playback/iplaybackaudioservice.h"
#include "domain/playback/playbacksession.h"
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
#include <QThread>
#include <QTemporaryDir>

#include <cstdio>
#include <cstdlib>
#include <memory>

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
        submittedGenerations.push_back(generation);
        submittedEvents += events;
    }
    void setEventGeneration(quint64 generation) override { currentGeneration = generation; }

    void clearSubmissions()
    {
        submittedEvents.clear();
        submittedGenerations.clear();
    }

    int startCount = 0;
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

} // namespace

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    testVisualizationRefreshRateSettingsNormalizeInts();
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
    testSeekPreservesTransportIntent();
    testSoundFontChangeRestoresPlayingAudioState();
    testSoundFontChangeFreezesAndRestoresTransport();
    testFailedSoundFontChangeRestoresTransport();
    std::fprintf(stdout, "playback session tests passed\n");
    return EXIT_SUCCESS;
}
