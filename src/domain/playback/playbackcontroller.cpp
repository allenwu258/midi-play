#include "playbackcontroller.h"

namespace midi_play::playback {

PlaybackController::PlaybackController(QObject* parent)
    : QObject(parent)
{
}

bool PlaybackController::setDocument(std::shared_ptr<const music::MusicDocument> document,
                                     std::unique_ptr<IPlaybackAudioService> audioService,
                                     QString* error)
{
    if (!document || !document->isValid() || !audioService) {
        if (error) *error = QStringLiteral("无法创建无效的播放会话");
        return false;
    }
    m_session = std::make_unique<PlaybackSession>(std::move(document), std::move(audioService));
    connect(m_session.get(), &PlaybackSession::stateChanged, this, &PlaybackController::stateChanged);
    connect(m_session.get(), &PlaybackSession::positionChanged, this, &PlaybackController::positionChanged);
    connect(m_session.get(), &PlaybackSession::errorOccurred, this, &PlaybackController::errorOccurred);
    return true;
}

bool PlaybackController::loadSoundFont(const QString& path, QString* error)
{
    return m_session && m_session->loadSoundFont(path, error);
}

void PlaybackController::play() { if (m_session) m_session->play(); }
void PlaybackController::pause() { if (m_session) m_session->pause(); }
void PlaybackController::stop() { if (m_session) m_session->stop(); }
void PlaybackController::seek(qint64 microseconds) { if (m_session) m_session->seek(microseconds); }

} // namespace midi_play::playback
