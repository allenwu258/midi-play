#include "playerapplicationservice.h"

#include "infrastructure/audio/fluidsynthengine.h"
#include "infrastructure/audio/fluidsynthaudioservice.h"
#include "infrastructure/audio/threadedplaybackaudioservice.h"
#include "infrastructure/readers/musicxmlreaderadapter.h"
#include "infrastructure/readers/midireaderadapter.h"

#include <QFileInfo>
#include <QtConcurrent>

namespace midi_play::app {

PlayerApplicationService::PlayerApplicationService(QObject* parent)
    : QObject(parent)
{
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
    emit busyChanged(true);
    auto watcher = new QFutureWatcher<music::ReadResult>(this);
    connect(watcher, &QFutureWatcher<music::ReadResult>::finished, this, [this, watcher, path] {
        const auto result = watcher->result();
        watcher->deleteLater();
        emit busyChanged(false);
        if (!result.ok()) {
            emit errorOccurred(result.error);
            return;
        }
        auto engine = std::make_unique<audio::FluidSynthEngine>();
        auto fluidsynthService = std::make_unique<audio::FluidSynthAudioService>(std::move(engine));
        auto audioService = std::make_unique<audio::ThreadedPlaybackAudioService>(std::move(fluidsynthService));
        m_controller = std::make_unique<playback::PlaybackController>(this);
        QString controllerError;
        if (!m_controller->setDocument(result.document, std::move(audioService), &controllerError)) {
            emit errorOccurred(controllerError);
            return;
        }
        m_fileName = path;
        connectSession();
        if (!m_soundFontPath.isEmpty()) {
            loadSoundFont(m_soundFontPath);
        }
        emit documentLoaded(result.document->title().isEmpty() ? QFileInfo(path).fileName() : result.document->title(),
                            session()->durationMicroseconds());
    });
    watcher->setFuture(QtConcurrent::run([reader, path] { return reader->read(path); }));
}

void PlayerApplicationService::loadSoundFont(const QString& path)
{
    m_soundFontPath = path;
    if (!session()) {
        emit soundFontLoaded(path);
        return;
    }
    QString error;
    if (!m_controller->loadSoundFont(path, &error)) {
        emit errorOccurred(error);
        return;
    }
    emit soundFontLoaded(path);
}

void PlayerApplicationService::play() { if (m_controller) m_controller->play(); }
void PlayerApplicationService::pause() { if (m_controller) m_controller->pause(); }
void PlayerApplicationService::stop() { if (m_controller) m_controller->stop(); }
void PlayerApplicationService::seek(qint64 microseconds) { if (m_controller) m_controller->seek(microseconds); }

void PlayerApplicationService::connectSession()
{
    connect(m_controller.get(), &playback::PlaybackController::stateChanged, this, [this](playback::State state) {
        Q_UNUSED(state)
    });
    connect(m_controller.get(), &playback::PlaybackController::errorOccurred, this, &PlayerApplicationService::errorOccurred);
    connect(m_controller.get(), &playback::PlaybackController::positionChanged, this, &PlayerApplicationService::positionChanged);
}

} // namespace midi_play::app
