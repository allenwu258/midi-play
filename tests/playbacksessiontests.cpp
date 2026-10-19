// 播放会话测试：覆盖设置规范化、事件时间轴、时钟倍率、seek、音源切换及节拍器生命周期。
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
    // 统一失败出口，确保异步会话测试返回明确的非零状态。
    std::fprintf(stderr, "FAILED: %s\n", message);
    std::exit(EXIT_FAILURE);
}

void require(bool condition, const char* message)
{
    // 所有同步和异步断言都汇入同一失败路径，避免遗漏非零退出码。
    if (!condition) fail(message);
}

class RecordingAudioService final : public IPlaybackAudioService {
    // 可记录调用顺序和可控失败/延迟的音频替身，隔离真实设备与线程实现。
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
    // 内存设置存储用于观察读写次数、持久化快照和可控错误。
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

// 刷新率输入按整数范围规范化，非法值回退默认值并保持周期可计算。
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

void testBackgroundImageMaximumDimensionSettingsNormalizeInts()
{
    using namespace midi_play::settings;
    require(normalizeBackgroundImageMaximumDimension(256) == 256,
            "minimum background dimension must be accepted");
    require(normalizeBackgroundImageMaximumDimension(2048) == 2048,
            "default background dimension must be accepted");
    require(normalizeBackgroundImageMaximumDimension(4096) == 4096,
            "maximum background dimension must be accepted");
    require(normalizeBackgroundImageMaximumDimension(255) == kDefaultBackgroundImageMaximumDimension
                && normalizeBackgroundImageMaximumDimension(4097) == kDefaultBackgroundImageMaximumDimension,
            "out-of-range background dimensions must fall back to 2048");
}

// 图形模式首次确认需持久化，传统后端同时关闭 Vulkan 特效。
void testGraphicsModeSettings()
{
    QTemporaryDir temporary;
    require(temporary.isValid(), "temporary graphics settings directory must be available");
    const auto settingsPath = temporary.filePath(QStringLiteral("settings.ini"));
    midi_play::infrastructure::settings::QSettingsStore freshStore(settingsPath);
    const auto freshSettings = freshStore.load(nullptr);
    require(freshSettings.graphicsMode == midi_play::settings::GraphicsMode::VulkanExperimental,
            "new settings must prefer Vulkan");
    require(!freshSettings.graphicsModeConfigured,
            "a missing graphics mode key must be treated as unconfigured");

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
    require(rawStore->savedSettings.graphicsModeConfigured,
            "resolving a graphics mode must mark the preference configured");

    midi_play::app::SettingsService fallbackService(
        std::make_unique<midi_play::infrastructure::settings::QSettingsStore>(settingsPath));
    fallbackService.load();
    require(!fallbackService.graphicsModeConfigured(),
            "a new settings file must still be unconfigured before renderer resolution");
    fallbackService.setGraphicsMode(midi_play::settings::GraphicsMode::Traditional);
    require(fallbackService.graphicsModeConfigured(),
            "traditional fallback must become a persisted preference");
    const auto persistedFallback = freshStore.load(nullptr);
    require(persistedFallback.graphicsMode == midi_play::settings::GraphicsMode::Traditional
                && persistedFallback.graphicsModeConfigured,
            "traditional fallback must persist for the next launch");
}

// 标题栏模式遵守平台能力，非 Windows 构建不能恢复自定义模式。
void testTitleBarModePlatformPolicy()
{
    require(midi_play::settings::normalizeTitleBarMode(
                midi_play::settings::TitleBarMode::Native)
                == midi_play::settings::TitleBarMode::Native,
            "native title bar mode must always be valid");
#if defined(Q_OS_WIN)
    require(midi_play::settings::kDefaultTitleBarMode
                == midi_play::settings::TitleBarMode::Custom,
            "Windows must default to the custom title bar");
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

// 标题栏变更先发布有效值，再验证存储快照中的持久化整数。
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

// 音源路径变更只在值实际变化时保存并发送信号。
void testSettingsServicePersistsSoundFont()
{
    auto store = std::make_unique<MemorySettingsStore>();
    auto* rawStore = store.get();
    const QString customPath = QDir::cleanPath(
        QDir::temp().absoluteFilePath(QStringLiteral("midi-play/custom/orchestra.sf2")));
    midi_play::app::SettingsService service(std::move(store));
    service.load();

    require(service.soundFontPath().isEmpty(), "new users must have no implicit SoundFont");

    int changeCount = 0;
    QString changedPath;
    QObject::connect(&service, &midi_play::app::SettingsService::soundFontPathChanged,
                     [&](const QString& path) {
                         ++changeCount;
                         changedPath = path;
                     });

    service.setSoundFontPath(customPath);
    require(service.soundFontPath() == customPath,
            "custom SoundFont path must be normalized and exposed");
    require(rawStore->saveCount == 1
                && rawStore->savedSettings.soundFontPath == customPath,
            "custom SoundFont override must be persisted");
    require(changeCount == 1 && changedPath == customPath,
            "custom SoundFont change must publish its effective state");

    service.setSoundFontPath({});
    require(service.soundFontPath().isEmpty(), "clearing the path leaves the player unconfigured");
    require(rawStore->saveCount == 2
                && rawStore->savedSettings.soundFontPath.isEmpty(),
            "clearing a SoundFont must persist an empty path");
    require(changeCount == 2 && changedPath.isEmpty(), "clearing must publish the unconfigured state");

    service.setSoundFontPath({});
    require(rawStore->saveCount == 2 && changeCount == 2,
            "an unchanged empty path must not emit or write again");
}

// 背景路径、启用状态、适配和透明度分别保存且按规范化值发布。
void testSettingsServicePersistsBackgroundImage()
{
    auto store = std::make_unique<MemorySettingsStore>();
    auto* rawStore = store.get();
    midi_play::app::SettingsService service(std::move(store));
    service.load();

    const QString path = QDir::cleanPath(
        QDir::temp().absoluteFilePath(QStringLiteral("midi-play/background/image.png")));
    int changes = 0;
    int modeChanges = 0;
    QObject::connect(&service, &midi_play::app::SettingsService::backgroundImagePathChanged,
                     [&changes](const QString&) { ++changes; });
    QObject::connect(&service, &midi_play::app::SettingsService::backgroundImageEnabledChanged,
                     [&modeChanges](bool) { ++modeChanges; });

    service.setBackgroundImagePath(path);
    require(service.backgroundImagePath() == path
                && rawStore->savedSettings.backgroundImagePath == path
                && rawStore->saveCount == 1 && changes == 1,
            "background image path must be normalized, published and persisted");
    require(service.activeBackgroundImagePath().isEmpty(),
            "a selected image must remain inactive until image mode is enabled");

    service.setBackgroundImageEnabled(true);
    require(service.backgroundImageEnabled() && service.activeBackgroundImagePath() == path
                && rawStore->savedSettings.backgroundImageEnabled
                && rawStore->saveCount == 2 && modeChanges == 1,
            "enabling image mode must persist and activate the selected image");

    service.setBackgroundImageEnabled(false);
    require(!service.backgroundImageEnabled() && service.activeBackgroundImagePath().isEmpty()
                && service.backgroundImagePath() == path && rawStore->savedSettings.backgroundImagePath == path
                && rawStore->saveCount == 3 && modeChanges == 2,
            "disabling image mode must preserve the image selection");
    service.setBackgroundImageEnabled(false);
    require(rawStore->saveCount == 3 && modeChanges == 2,
            "an unchanged background mode must not emit or persist again");

    service.setBackgroundImagePath(path);
    require(rawStore->saveCount == 3 && changes == 1,
            "an unchanged background image path must not be saved again");

    service.setBackgroundImagePath({});
    require(service.backgroundImagePath().isEmpty()
                && rawStore->savedSettings.backgroundImagePath.isEmpty()
                && rawStore->saveCount == 4 && changes == 2,
            "clearing the background image must persist an empty path");
}

void testSettingsServicePersistsBackgroundImageMaximumDimension()
{
    auto store = std::make_unique<MemorySettingsStore>();
    auto* rawStore = store.get();
    midi_play::app::SettingsService service(std::move(store));
    service.load();
    require(service.backgroundImageMaximumDimension() == midi_play::settings::kDefaultBackgroundImageMaximumDimension,
            "background dimension must default to 2048");

    int changes = 0;
    QObject::connect(&service, &midi_play::app::SettingsService::backgroundImageMaximumDimensionChanged,
                     [&changes](int) { ++changes; });
    service.setBackgroundImageMaximumDimension(4096);
    require(service.backgroundImageMaximumDimension() == 4096
                && rawStore->savedSettings.backgroundImageMaximumDimension == 4096
                && changes == 1,
            "background dimension changes must be normalized, published and persisted");
    service.setBackgroundImageMaximumDimension(4097);
    require(service.backgroundImageMaximumDimension() == midi_play::settings::kDefaultBackgroundImageMaximumDimension
                && rawStore->savedSettings.backgroundImageMaximumDimension
                    == midi_play::settings::kDefaultBackgroundImageMaximumDimension
                && changes == 2,
            "invalid background dimensions must fall back to 2048");
    service.setBackgroundImageMaximumDimension(2048);
    require(rawStore->saveCount == 2 && changes == 2,
            "setting the already effective background dimension must not save again");
    service.setBackgroundImageMaximumDimension(2048);
    require(rawStore->saveCount == 2 && changes == 2,
            "unchanged background dimension must not save again");
}

// 重复设置相同值不应产生额外信号或写入，保存失败需独立报告。
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

// 使用临时 INI 验证 QSettingsStore 的刷新率读写和警告行为。
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
#if defined(Q_OS_WIN)
    require(loaded.titleBarMode == midi_play::settings::TitleBarMode::Custom,
            "missing Windows title bar preference must use the custom default");
#else
    require(loaded.titleBarMode == midi_play::settings::TitleBarMode::Native,
            "missing non-Windows title bar preference must use the native default");
#endif

    PlayerSettings saved;
    saved.visualizationRefreshRate = 120;
    saved.titleBarMode = midi_play::settings::TitleBarMode::Custom;
    saved.soundFontPath = QStringLiteral("C:/SoundFonts/custom.sf2");
    saved.backgroundImagePath = QStringLiteral("C:/Pictures/falling-notes.png");
    saved.backgroundImageEnabled = true;
    saved.backgroundImageAlignment = midi_play::settings::BackgroundImageAlignment::Right;
    saved.backgroundImageOpacity = 60;
    saved.backgroundImageMaximumDimension = 4096;
    QString error;
    require(store.save(saved, &error), "settings store must save a valid refresh rate");
    require(error.isEmpty(), "successful settings save must not report an error");

    loaded = store.load(&warning);
    require(loaded.visualizationRefreshRate == 120,
            "settings store must reload the persisted refresh rate");
    require(loaded.soundFontPath == saved.soundFontPath,
            "settings store must reload the custom SoundFont override");
    require(loaded.backgroundImagePath == saved.backgroundImagePath,
            "settings store must reload the custom background image");
    require(loaded.backgroundImageEnabled,
            "settings store must reload image background mode");
    require(loaded.backgroundImageAlignment == midi_play::settings::BackgroundImageAlignment::Right
                && loaded.backgroundImageOpacity == 60
                && loaded.backgroundImageMaximumDimension == 4096,
            "settings store must reload background placement, opacity and dimension");
    {
        QSettings legacy(settingsPath, QSettings::IniFormat);
        legacy.remove(QStringLiteral("Visualization/backgroundImageEnabled"));
        legacy.sync();
    }
    require(store.load(&warning).backgroundImageEnabled,
            "legacy settings with a background image must keep it enabled");
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
    file.setValue(QStringLiteral("Visualization/backgroundImageMaximumDimension"), 4097);
    file.sync();
    loaded = store.load(&warning);
    require(loaded.visualizationRefreshRate == 60,
            "out-of-range persisted refresh rate must fall back to 60");
    require(!warning.isEmpty(), "invalid persisted refresh rate should report a warning");
    require(loaded.titleBarMode == midi_play::settings::TitleBarMode::Native,
            "invalid persisted title bar mode must fall back to native");
    require(loaded.backgroundImageMaximumDimension
                == midi_play::settings::kDefaultBackgroundImageMaximumDimension,
            "invalid persisted background dimension must fall back to 2048");

    loaded.soundFontPath.clear();
    loaded.backgroundImagePath.clear();
    loaded.backgroundImageEnabled = false;
    require(store.save(loaded, &error),
            "settings store must save a reset SoundFont configuration");
    QSettings resetFile(settingsPath, QSettings::IniFormat);
    require(!resetFile.contains(QStringLiteral("Audio/soundFontPath")),
            "reset must remove the custom SoundFont key from the settings file");
    require(!resetFile.contains(QStringLiteral("Visualization/backgroundImagePath")),
            "reset must remove the custom background image key from the settings file");
    require(!resetFile.value(QStringLiteral("Visualization/backgroundImageEnabled")).toBool(),
            "reset must disable image background mode");
}

// 重复展开只构建一次缓存，后续 PlaybackContext 复用同一绝对时间线。
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

// 线程音频服务返回能力快照，外部修改不会改变内部设备能力。
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

// PlaybackContext 将重复段中的力度/音色变化投影到每次实际播放实例。
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

// 有效原子时钟快照驱动位置采样，跨线程读取不依赖 GUI 状态。
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

// 缺少时钟快照时安全回退到离散位置，不产生未初始化读数。
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
    document->setDuration(1'920); // 120 BPM 下约两秒。

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

// 只有后端声明时钟能力时，线程服务才发布可外推的采样时间。
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

// seek 在播放、暂停和停止状态下保留用户传输意图并清理旧事件。
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

// 播放中切换音源冻结旧会话，成功加载后恢复播放状态和位置。
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

// 音源切换期间暂停音频提交，完成后恢复原先的暂停/播放传输状态。
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

// 音源加载失败应恢复原音源和传输状态，并发布可操作错误。
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

// 位置节流器丢弃旧采样，只发布截止时间前最新且有意义的值。
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

// 播放倍率切换保持当前位置连续，新的墙钟斜率从切换点开始生效。
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

// 会话倍率变化不重建音频轨道，继续沿用当前声音和传输状态。
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

// 倍率重建事件时保持 NoteOn/NoteOff 顺序、音高和代次隔离。
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
    note.start = 48; // 50 ms 的音乐时间。
    note.duration = 144; // 在 300 ms 处结束，跨越速度变化点。
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

// 控制器在 GUI 线程排队应用倍率，新会话继承已选倍率。
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

// 不支持倍率的后端必须显式拒绝，不能静默保持错误的播放速度。
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


// 节拍器随播放/暂停/seek 启停，重拍标记和事件代次与运输状态一致。
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

// 节拍器准备失败时保留用户偏好但禁用能力，后续成功会话重新评估。
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
    // 前一次阻塞加载已投递能力信号，但尚未到达 GUI 队列；先替换会话再派发该信号。
    require(controller.setDocument(testDocument(), std::make_unique<RecordingAudioService>(), nullptr),
            "replace supported session before its queued notification arrives");
    processEventsFor(10);
    require(!controller.supportsMetronome(), "old session availability cannot overwrite a replacement song");
}

// 音源/节拍器异步任务取消后不再提交旧点击，也能安全结束线程。
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
    threaded.start(); // 等待工作线程屏障，确保所有排队任务已完成后再检查。
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

// 主入口按领域能力分组执行全部会话回归，不需要真实音频设备。
int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    testVisualizationRefreshRateSettingsNormalizeInts();
    testBackgroundImageMaximumDimensionSettingsNormalizeInts();
    testGraphicsModeSettings();
    testTitleBarModePlatformPolicy();
    testSettingsServicePersistsTitleBarMode();
    testSettingsServicePersistsSoundFont();
    testSettingsServicePersistsBackgroundImage();
    testSettingsServicePersistsBackgroundImageMaximumDimension();
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
