#include "playerapplicationservice.h"

#include "infrastructure/audio/fluidsynthengine.h"
#include "infrastructure/audio/fluidsynthaudioservice.h"
#include "infrastructure/musicxml/musicxmlreader.h"

#include <QFileInfo>
#include <QtConcurrent>

namespace midi_play::app {

PlayerApplicationService::PlayerApplicationService(QObject* parent)
    : QObject(parent)
{
}

void PlayerApplicationService::openMusicXml(const QString& path)
{
    emit busyChanged(true);
    auto watcher = new QFutureWatcher<musicxml::ReadResult>(this);
    connect(watcher, &QFutureWatcher<musicxml::ReadResult>::finished, this, [this, watcher, path] {
        const auto result = watcher->result();
        watcher->deleteLater();
        emit busyChanged(false);
        if (!result.ok()) {
            emit errorOccurred(result.error);
            return;
        }
        auto engine = std::make_unique<audio::FluidSynthEngine>();
        auto audioService = std::make_unique<audio::FluidSynthAudioService>(std::move(engine));
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
    watcher->setFuture(QtConcurrent::run([path] { return musicxml::MusicXmlReader().read(path); }));
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
