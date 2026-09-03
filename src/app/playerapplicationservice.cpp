#include "playerapplicationservice.h"

#include "infrastructure/audio/fluidsynthengine.h"
#include "infrastructure/audio/fluidsynthaudioservice.h"
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
}

void PlayerApplicationService::openMusicXml(const QString& path)
{
    openFile(path);
}

void PlayerApplicationService::openFile(const QString& path)
{
    const auto suffix = QFileInfo(path).suffix();
    const auto* reader = m_readerRegistry.find(suffix);
    if (!reader) {
        emit errorOccurred(QStringLiteral("不支持的音乐文件类型: %1").arg(suffix));
        return;
    }
    const quint64 generation = ++m_loadGeneration;
    emit busyChanged(true);
    auto watcher = new QFutureWatcher<LoadedProject>(this);
    connect(watcher, &QFutureWatcher<LoadedProject>::finished, this, [this, watcher, path, generation] {
        const auto result = watcher->result();
        watcher->deleteLater();
        if (generation != m_loadGeneration) return;
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
        QString controllerError;
        if (!newController->setDocument(result.readResult.document, std::move(audioService),
                                        &controllerError)) {
            emit errorOccurred(controllerError);
            return;
        }
        if (m_soundFontPath.isEmpty()) {
            emit errorOccurred(QStringLiteral("没有可用的音源，无法建立播放会话"));
            return;
        }
        if (!newController->loadSoundFont(m_soundFontPath, &controllerError)) {
            emit errorOccurred(QStringLiteral("无法为乐曲加载音源: %1").arg(controllerError));
            return;
        }

        m_controller = std::move(newController);
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

bool PlayerApplicationService::loadSoundFont(const QString& path)
{
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
        emit soundFontLoaded(normalizedPath);
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
    emit soundFontLoaded(normalizedPath);
    return true;
}

bool PlayerApplicationService::validateSoundFontFile(const QString& path,
                                                     QString* normalizedPath)
{
    const QFileInfo soundFontInfo(path);
    if (path.trimmed().isEmpty() || !soundFontInfo.exists() || !soundFontInfo.isFile()
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
    emit soundFontLoadFailed(message.isEmpty()
        ? QStringLiteral("音源加载失败") : message);
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

void PlayerApplicationService::play() { if (m_controller) m_controller->play(); }
void PlayerApplicationService::pause() { if (m_controller) m_controller->pause(); }
void PlayerApplicationService::stop() { if (m_controller) m_controller->stop(); }
void PlayerApplicationService::seek(qint64 microseconds) { if (m_controller) m_controller->seek(microseconds); }

void PlayerApplicationService::connectSession()
{
    connect(m_controller.get(), &playback::PlaybackController::stateChanged, this, [this](playback::State state) {
        m_playbackState = state;
        emit playbackStateChanged(state);
    });
    connect(m_controller.get(), &playback::PlaybackController::errorOccurred, this, &PlayerApplicationService::errorOccurred);
    connect(m_controller.get(), &playback::PlaybackController::positionChanged, this,
            [this](qint64 position, qint64 duration) {
                m_positionUs = position;
                m_durationUs = duration;
                emit positionChanged(position, duration);
            });
}

} // namespace midi_play::app
