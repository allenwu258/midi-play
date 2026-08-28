#include "playbackcontroller.h"

#include <QMetaObject>
#include <QThread>

namespace midi_play::playback {
namespace {

constexpr int kPositionPublishIntervalMs = 16;

} // namespace

PlaybackController::PlaybackController(QObject* parent)
    : QObject(parent)
{
    m_playbackThread.setObjectName(QStringLiteral("PlaybackThread"));
    m_positionTimer.setInterval(kPositionPublishIntervalMs);
    m_positionTimer.setTimerType(Qt::PreciseTimer);
    connect(&m_positionTimer, &QTimer::timeout, this, &PlaybackController::onPositionTimer);
}

PlaybackController::~PlaybackController()
{
    m_positionTimer.stop();
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
    m_positionTimer.stop();
    m_positionThrottler.reset();
    m_session = std::make_unique<PlaybackSession>(std::move(document), std::move(audioService));
    m_playbackThread.start();
    m_session->moveToThread(&m_playbackThread);
    connect(m_session.get(), &PlaybackSession::stateChanged, this,
            [this](State state) {
                // State transitions are transport boundaries. Flush the
                // latest sample before forwarding the state so pause/stop
                // cannot leave the UI one throttling interval behind.
                flushPositionUpdate();
                if (state == State::Playing) {
                    m_positionTimer.start();
                } else {
                    m_positionTimer.stop();
                }
                emit stateChanged(state);
            });
    // Position samples are deliberately received directly from the playback
    // thread and coalesced into the controller's 60 Hz timer. This keeps the
    // scheduler's 2 ms cadence independent from GUI event delivery.
    connect(m_session.get(), &PlaybackSession::positionChanged, this,
            [this](qint64 position, qint64 duration) {
                m_positionThrottler.publish(position, duration);
            }, Qt::DirectConnection);
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
        // This branch runs on the playback thread. PositionThrottler is a
        // single-consumer handoff, so never consume it here; queue the flush
        // to the controller's owning thread instead.
        QMetaObject::invokeMethod(this, [this] {
            flushPositionUpdate();
        }, Qt::QueuedConnection);
        return;
    }
    QMetaObject::invokeMethod(m_session.get(), "seek", Qt::BlockingQueuedConnection,
                              Q_ARG(qint64, microseconds));
    // The blocking seek has completed the transport transaction. Publish its
    // exact endpoint immediately instead of waiting for the next 16 ms tick.
    flushPositionUpdate();
}

void PlaybackController::onPositionTimer()
{
    flushPositionUpdate();
}

void PlaybackController::flushPositionUpdate()
{
    PlaybackPositionThrottler::Snapshot snapshot;
    if (!m_positionThrottler.takeLatest(snapshot)) {
        return;
    }
    emit positionChanged(snapshot.positionUs, snapshot.durationUs);
}

} // namespace midi_play::playback
