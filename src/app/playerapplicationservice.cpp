#include "playerapplicationservice.h"

#include "infrastructure/audio/fluidsynthengine.h"
#include "infrastructure/audio/fluidsynthaudioservice.h"
#include "infrastructure/audio/soundfontinspector.h"
#include "infrastructure/audio/threadedplaybackaudioservice.h"
#include "infrastructure/readers/musicxmlreaderadapter.h"
#include "infrastructure/readers/midireaderadapter.h"
#include "domain/visualization/playbackvisualizationprojector.h"

#include <QFileInfo>
#include <QtConcurrent>

namespace midi_play::app {
namespace {

struct LoadedProject {
    music::ReadResult readResult;
    visualization::VisualChartPtr visualChart;
    QString visualizationError;
    quint64 generation = 0;
};

} // namespace

PlayerApplicationService::PlayerApplicationService(QObject* parent)
    : QObject(parent)
{
    qRegisterMetaType<playback::State>();
    qRegisterMetaType<visualization::VisualChartPtr>();
    m_readerRegistry.registerReader(std::make_unique<readers::MusicXmlReaderAdapter>());
    m_readerRegistry.registerReader(std::make_unique<readers::MidiReaderAdapter>());
    connect(&m_soundFontValidationWatcher, &QFutureWatcher<QString>::finished, this, [this] {
        const QString validationError = m_soundFontValidationWatcher.result();
        if (!validationError.isEmpty()) {
            completeSoundFontLoad(false, validationError);
            return;
        }
        completeSoundFontLoad(true, {});
    });
}

void PlayerApplicationService::openMusicXml(const QString& path)
{
    openFile(path);
}

void PlayerApplicationService::openFile(const QString& path)
{
    if (m_soundFontLoading) {
        emit errorOccurred(QStringLiteral("音源加载仍在进行中，请稍后再打开乐曲"));
        return;
    }
    const auto suffix = QFileInfo(path).suffix();
    const auto* reader = m_readerRegistry.find(suffix);
    if (!reader) {
        emit errorOccurred(QStringLiteral("不支持的音乐文件类型: %1").arg(suffix));
        return;
    }
    const quint64 generation = ++m_loadGeneration;
    m_documentLoading = true;
    emit busyChanged(true);
    auto watcher = new QFutureWatcher<LoadedProject>(this);
    connect(watcher, &QFutureWatcher<LoadedProject>::finished, this, [this, watcher, path, generation] {
        const auto result = watcher->result();
        watcher->deleteLater();
        if (generation != m_loadGeneration) return;
        m_documentLoading = false;
        emit busyChanged(false);
        if (!result.readResult.ok()) {
            emit errorOccurred(result.readResult.error);
            return;
        }
        if (!result.visualChart) {
            emit errorOccurred(result.visualizationError.isEmpty()
                ? QStringLiteral("无法建立播放可视化") : result.visualizationError);
            return;
        }
        auto engine = std::make_unique<audio::FluidSynthEngine>();
        auto fluidsynthService = std::make_unique<audio::FluidSynthAudioService>(std::move(engine));
        auto audioService = std::make_unique<audio::ThreadedPlaybackAudioService>(std::move(fluidsynthService));
        auto newController = std::make_unique<playback::PlaybackController>(this);
        newController->setPositionPublishRate(m_visualizationRefreshRate);
        newController->setPlaybackRatePercent(m_playbackRatePercent);
        newController->setMetronomeEnabled(m_metronomeEnabled);
        QString controllerError;
        if (!newController->setDocument(result.readResult.document, std::move(audioService),
                                        &controllerError)) {
            emit errorOccurred(controllerError);
            return;
        }
        QString soundFontError;
        if (!m_soundFontPath.isEmpty()
            && !newController->loadSoundFont(m_soundFontPath, &soundFontError)) {
            // Keep the parsed score available even when the external font has
            // disappeared. A later selection can initialize this same session.
            m_soundFontPath.clear();
            if (soundFontError.isEmpty()) soundFontError = QStringLiteral("无法为乐曲加载音源");
        }

        m_controller = std::move(newController);
        emit metronomeAvailabilityChanged(m_controller->supportsMetronome(),
                                          m_controller->metronomeUnavailableReason());
        m_fileName = path;
        connectSession();
        m_positionUs = 0;
        m_durationUs = session()->durationMicroseconds();
        m_playbackState = playback::State::Ready;
        emit visualizationReady(result.visualChart);
        emit documentLoaded(result.readResult.document->title().isEmpty()
                                ? QFileInfo(path).fileName() : result.readResult.document->title(),
                            m_durationUs);
        emit positionChanged(m_positionUs, m_durationUs);
        emit playbackStateChanged(m_playbackState);
        if (!soundFontError.isEmpty()) reportSoundFontFailure(soundFontError);
    });
    watcher->setFuture(QtConcurrent::run([reader, path, generation] {
        LoadedProject result;
        result.generation = generation;
        result.readResult = reader->read(path);
        if (result.readResult.ok()) {
            visualization::VisualizationProjectionOptions options;
            options.fallbackTitle = QFileInfo(path).completeBaseName();
            result.visualChart = visualization::PlaybackVisualizationProjector().project(
                *result.readResult.document, generation, options, &result.visualizationError);
        }
        return result;
    }));
}

void PlayerApplicationService::requestSoundFontLoad(const QString& path)
{
    if (m_soundFontLoading || m_documentLoading) {
        reportSoundFontFailure(QStringLiteral("正在加载资源，请稍后再选择音源"));
        return;
    }

    QString normalizedPath;
    if (!normalizeSoundFontPath(path, &normalizedPath)) {
        emit soundFontLoadFinished(false);
        return;
    }

    m_pendingSoundFontPath = normalizedPath;
    m_lastSoundFontError.clear();
    setSoundFontLoading(true);
    if (m_controller) {
        m_controller->loadSoundFontAsync(normalizedPath);
        return;
    }

    m_soundFontValidationWatcher.setFuture(QtConcurrent::run([normalizedPath] {
        audio::FluidSynthEngine validator;
        QString error;
        if (validator.validateSoundFont(normalizedPath, &error)) return QString();
        return error.isEmpty() ? QStringLiteral("音源验证失败") : error;
    }));
}

bool PlayerApplicationService::loadSoundFont(const QString& path)
{
    if (m_soundFontLoading || m_documentLoading) {
        reportSoundFontFailure(QStringLiteral("正在加载资源，请稍后再选择音源"));
        return false;
    }
    QString normalizedPath;
    if (!validateSoundFontFile(path, &normalizedPath)) {
        return false;
    }

    if (!session()) {
        audio::FluidSynthEngine validator;
        QString validationError;
        if (!validator.validateSoundFont(normalizedPath, &validationError)) {
            reportSoundFontFailure(validationError);
            return false;
        }
        m_soundFontPath = normalizedPath;
        m_lastSoundFontError.clear();
        emit soundFontLoaded(normalizedPath);
        emit soundFontSelectionCommitted(normalizedPath);
        return true;
    }

    const QString previousPath = m_soundFontPath;
    QString error;
    if (!m_controller->loadSoundFont(normalizedPath, &error)) {
        QString recoveryError;
        if (!previousPath.isEmpty() && previousPath != normalizedPath
            && !m_controller->loadSoundFont(previousPath, &recoveryError)) {
            error += QStringLiteral("；恢复原音源失败: %1").arg(recoveryError);
        }
        reportSoundFontFailure(error);
        return false;
    }
    m_soundFontPath = normalizedPath;
    m_lastSoundFontError.clear();
    emit soundFontLoaded(normalizedPath);
    emit soundFontSelectionCommitted(normalizedPath);
    return true;
}

bool PlayerApplicationService::validateSoundFontFile(const QString& path,
                                                     QString* normalizedPath)
{
    QString normalized;
    if (!normalizeSoundFontPath(path, &normalized)) {
        return false;
    }

    QString inspectionError;
    const auto inspection = audio::SoundFontInspector::inspect(normalized,
                                                                &inspectionError);
    if (!inspection.validRiffContainer) {
        reportSoundFontFailure(inspectionError.isEmpty()
            ? QStringLiteral("音源文件容器无效: %1").arg(path) : inspectionError);
        return false;
    }

    if (normalizedPath) {
        *normalizedPath = normalized;
    }
    return true;
}

bool PlayerApplicationService::normalizeSoundFontPath(const QString& path,
                                                      QString* normalizedPath)
{
    if (path.trimmed().isEmpty()) {
        reportSoundFontFailure(QStringLiteral("尚未配置有效音源，请在设置中选择 SF2/SF3 音源后播放。"));
        return false;
    }
    const QFileInfo soundFontInfo(path.trimmed());
    if (!soundFontInfo.exists() || !soundFontInfo.isFile()
        || !soundFontInfo.isReadable()) {
        reportSoundFontFailure(QStringLiteral("无法读取音源文件: %1").arg(path));
        return false;
    }

    const QString suffix = soundFontInfo.suffix().toLower();
    if (suffix != QStringLiteral("sf2") && suffix != QStringLiteral("sf3")) {
        reportSoundFontFailure(QStringLiteral("不支持的音源文件类型: %1").arg(suffix));
        return false;
    }

    if (normalizedPath) {
        *normalizedPath = soundFontInfo.absoluteFilePath();
    }
    return true;
}

void PlayerApplicationService::reportSoundFontFailure(const QString& message)
{
    m_lastSoundFontError = message.isEmpty() ? QStringLiteral("音源加载失败") : message;
    emit soundFontLoadFailed(m_lastSoundFontError);
}

void PlayerApplicationService::setVisualizationRefreshRate(int refreshRate)
{
    const int normalizedRefreshRate = settings::normalizeVisualizationRefreshRate(refreshRate);
    if (m_visualizationRefreshRate == normalizedRefreshRate) {
        return;
    }

    m_visualizationRefreshRate = normalizedRefreshRate;
    if (m_controller) {
        m_controller->setPositionPublishRate(m_visualizationRefreshRate);
    }
}

void PlayerApplicationService::setPlaybackRatePercent(int percent)
{
    const int normalized = settings::normalizePlaybackRatePercent(percent);
    if (m_playbackRatePercent == normalized) return;

    if (m_controller && !m_controller->setPlaybackRatePercent(normalized)) {
        emit playbackRateChanged(m_playbackRatePercent);
        return;
    }
    m_playbackRatePercent = normalized;
    emit playbackRateChanged(normalized);
}

void PlayerApplicationService::setMetronomeEnabled(bool enabled)
{
    if (m_metronomeEnabled == enabled) return;
    if (m_controller) m_controller->setMetronomeEnabled(enabled);
    m_metronomeEnabled = enabled;
    emit metronomeChanged(enabled);
}

void PlayerApplicationService::setGraphicsMode(settings::GraphicsMode mode)
{
    m_graphicsMode = settings::normalizeGraphicsMode(mode);
    emit graphicsModeChanged(m_graphicsMode);
}

void PlayerApplicationService::play()
{
    if (m_soundFontLoading) {
        reportSoundFontFailure(QStringLiteral("正在检查或加载音源，请稍后再播放。"));
        return;
    }
    // Re-check readability/container on playback so moved or damaged external
    // files yield an actionable inline error instead of silent transport.
    if (!validateSoundFontFile(m_soundFontPath, nullptr)) return;
    if (m_controller) m_controller->play();
}
void PlayerApplicationService::pause() { if (m_controller) m_controller->pause(); }
void PlayerApplicationService::stop() { if (m_controller) m_controller->stop(); }
void PlayerApplicationService::seek(qint64 microseconds)
{
    if (!m_controller) return;
    m_controller->seek(microseconds);
    emit playbackDiscontinuity(m_positionUs);
}

void PlayerApplicationService::connectSession()
{
    connect(m_controller.get(), &playback::PlaybackController::metronomeAvailabilityChanged,
            this, &PlayerApplicationService::metronomeAvailabilityChanged);
    connect(m_controller.get(), &playback::PlaybackController::stateChanged, this, [this](playback::State state) {
        m_playbackState = state;
        emit playbackStateChanged(state);
    });
    connect(m_controller.get(), &playback::PlaybackController::errorOccurred, this, &PlayerApplicationService::errorOccurred);
    connect(m_controller.get(), &playback::PlaybackController::soundFontLoadFinished,
            this, &PlayerApplicationService::completeSoundFontLoad);
    connect(m_controller.get(), &playback::PlaybackController::positionChanged, this,
            [this](qint64 position, qint64 duration, qint64 sampledAtUs) {
                m_positionUs = position;
                m_durationUs = duration;
                emit positionChanged(position, duration, sampledAtUs);
            });
}

void PlayerApplicationService::completeSoundFontLoad(bool success, const QString& error)
{
    const QString path = m_pendingSoundFontPath;
    m_pendingSoundFontPath.clear();
    if (success) {
        m_soundFontPath = path;
        m_lastSoundFontError.clear();
    }
    setSoundFontLoading(false);
    if (!success) {
        reportSoundFontFailure(error);
        emit soundFontLoadFinished(false);
        return;
    }

    emit soundFontLoaded(path);
    emit soundFontSelectionCommitted(path);
    emit soundFontLoadFinished(true);
}

void PlayerApplicationService::setSoundFontLoading(bool loading)
{
    if (m_soundFontLoading == loading) {
        return;
    }
    m_soundFontLoading = loading;
    emit soundFontLoadingChanged(loading);
}

} // namespace midi_play::app
