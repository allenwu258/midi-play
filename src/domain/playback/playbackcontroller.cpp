#include "playbackcontroller.h"

#include <QMetaObject>
#include <QThread>

namespace midi_play::playback {

PlaybackController::PlaybackController(QObject* parent)
    : QObject(parent)
{
    m_playbackThread.setObjectName(QStringLiteral("PlaybackThread"));
}

PlaybackController::~PlaybackController()
{
    if (m_session) {
        QMetaObject::invokeMethod(m_session.get(), "stop", Qt::BlockingQueuedConnection);
    }
    m_playbackThread.quit();
    m_playbackThread.wait();
}

bool PlaybackController::setDocument(std::shared_ptr<const music::MusicDocument> document,
                                     std::unique_ptr<IPlaybackAudioService> audioService,
                                     QString* error)
{
    if (!document || !document->isValid() || !audioService) {
        if (error) *error = QStringLiteral("无法创建无效的播放会话");
        return false;
    }
    if (m_session) {
        QMetaObject::invokeMethod(m_session.get(), "stop", Qt::BlockingQueuedConnection);
        m_playbackThread.quit();
        m_playbackThread.wait();
        m_session.reset();
    }
    m_session = std::make_unique<PlaybackSession>(std::move(document), std::move(audioService));
    m_playbackThread.start();
    m_session->moveToThread(&m_playbackThread);
    connect(m_session.get(), &PlaybackSession::stateChanged, this, &PlaybackController::stateChanged);
    connect(m_session.get(), &PlaybackSession::positionChanged, this, &PlaybackController::positionChanged);
    connect(m_session.get(), &PlaybackSession::errorOccurred, this, &PlaybackController::errorOccurred);
    return true;
}

bool PlaybackController::loadSoundFont(const QString& path, QString* error)
{
    if (!m_session) return false;
    bool result = false;
    QString localError;
    QMetaObject::invokeMethod(m_session.get(), [this, &result, &localError, path] {
        result = m_session->loadSoundFont(path, &localError);
    }, Qt::BlockingQueuedConnection);
    if (error) *error = localError;
    return result;
}

void PlaybackController::play() { if (m_session) QMetaObject::invokeMethod(m_session.get(), "play", Qt::QueuedConnection); }
void PlaybackController::pause() { if (m_session) QMetaObject::invokeMethod(m_session.get(), "pause", Qt::QueuedConnection); }
void PlaybackController::stop() { if (m_session) QMetaObject::invokeMethod(m_session.get(), "stop", Qt::QueuedConnection); }
void PlaybackController::seek(qint64 microseconds)
{
    if (!m_session) {
        return;
    }

    // A seek changes the timer, playhead, scheduler and audio state as one
    // transport transaction. Wait until the session has applied it so a UI
    // release cannot race with stale position notifications.
    if (QThread::currentThread() == m_session->thread()) {
        m_session->seek(microseconds);
        return;
    }
    QMetaObject::invokeMethod(m_session.get(), "seek", Qt::BlockingQueuedConnection,
                              Q_ARG(qint64, microseconds));
}

} // namespace midi_play::playback
