#include "threadedplaybackaudioservice.h"

#include <QMetaObject>
#include <QPointer>

namespace midi_play::audio {

class ThreadedPlaybackAudioService::Worker final : public QObject {
public:
    explicit Worker(std::unique_ptr<playback::IPlaybackAudioService> service)
        : m_service(std::move(service)) {}

    std::unique_ptr<playback::IPlaybackAudioService> m_service;
    quint64 m_eventGeneration = 0;
};

ThreadedPlaybackAudioService::ThreadedPlaybackAudioService(std::unique_ptr<playback::IPlaybackAudioService> service)
    : m_capabilities(service ? service->capabilities()
                             : playback::PlaybackBackendCapabilities {})
    , m_worker(std::make_unique<Worker>(std::move(service)))
{
    m_worker->moveToThread(&m_thread);
    m_thread.start();
}

ThreadedPlaybackAudioService::~ThreadedPlaybackAudioService()
{
    m_thread.quit();
    m_thread.wait();
}

bool ThreadedPlaybackAudioService::loadSoundFont(const QString& path, QString* error)
{
    bool result = false;
    QString localError;
    QMetaObject::invokeMethod(m_worker.get(), [this, &result, &localError, path] {
        result = m_worker->m_service->loadSoundFont(path, &localError);
    }, Qt::BlockingQueuedConnection);
    if (error) *error = localError;
    return result;
}

bool ThreadedPlaybackAudioService::configureTrack(const QString& trackId, int channel, int program, QString* error)
{
    bool result = false;
    QString localError;
    QMetaObject::invokeMethod(m_worker.get(), [this, &result, &localError, trackId, channel, program] {
        result = m_worker->m_service->configureTrack(trackId, channel, program, &localError);
    }, Qt::BlockingQueuedConnection);
    if (error) *error = localError;
    return result;
}

bool ThreadedPlaybackAudioService::addTrack(const playback::PlaybackData& data, QString* error)
{
    bool result = false;
    QString localError;
    QMetaObject::invokeMethod(m_worker.get(), [this, &result, &localError, data] {
        result = m_worker->m_service->addTrack(data, &localError);
    }, Qt::BlockingQueuedConnection);
    if (error) *error = localError;
    return result;
}

bool ThreadedPlaybackAudioService::start()
{
    bool result = false;
    QMetaObject::invokeMethod(m_worker.get(), [this, &result] { result = m_worker->m_service->start(); }, Qt::BlockingQueuedConnection);
    return result;
}

bool ThreadedPlaybackAudioService::pause()
{
    bool result = false;
    QMetaObject::invokeMethod(m_worker.get(), [this, &result] { result = m_worker->m_service->pause(); }, Qt::BlockingQueuedConnection);
    return result;
}

bool ThreadedPlaybackAudioService::stop()
{
    bool result = false;
    QMetaObject::invokeMethod(m_worker.get(), [this, &result] { result = m_worker->m_service->stop(); }, Qt::BlockingQueuedConnection);
    return result;
}

bool ThreadedPlaybackAudioService::seek(qint64 positionUs)
{
    bool result = false;
    QMetaObject::invokeMethod(m_worker.get(), [this, &result, positionUs] { result = m_worker->m_service->seek(positionUs); }, Qt::BlockingQueuedConnection);
    return result;
}

bool ThreadedPlaybackAudioService::setTransportPosition(qint64 positionUs)
{
    bool result = false;
    QMetaObject::invokeMethod(m_worker.get(), [this, &result, positionUs] {
        result = m_worker->m_service->setTransportPosition(positionUs);
    }, Qt::BlockingQueuedConnection);
    return result;
}

qint64 ThreadedPlaybackAudioService::clockPositionUs() const
{
    // clockPositionUs() is explicitly a thread-safe snapshot API. Reading it
    // directly keeps an AudioDevice clock from adding a blocking worker hop to
    // every scheduler tick.
    return m_worker && m_worker->m_service
        ? m_worker->m_service->clockPositionUs()
        : -1;
}

playback::PlaybackClockSource ThreadedPlaybackAudioService::clockSource() const
{
    return m_capabilities.clockSource;
}

bool ThreadedPlaybackAudioService::supportsTimedEvents() const
{
    return m_capabilities.timedEvents;
}

bool ThreadedPlaybackAudioService::supportsPerNoteExpression() const
{
    return m_capabilities.perNoteExpression;
}

playback::PlaybackBackendCapabilities ThreadedPlaybackAudioService::capabilities() const
{
    return m_capabilities;
}

bool ThreadedPlaybackAudioService::flush()
{
    bool result = false;
    QMetaObject::invokeMethod(m_worker.get(), [this, &result] { result = m_worker->m_service->flush(); }, Qt::BlockingQueuedConnection);
    return result;
}

void ThreadedPlaybackAudioService::setEventGeneration(quint64 generation)
{
    m_submissionGeneration.store(generation, std::memory_order_release);
    QMetaObject::invokeMethod(m_worker.get(), [this, generation] {
        m_worker->m_eventGeneration = generation;
        m_worker->m_service->setEventGeneration(generation);
    }, Qt::BlockingQueuedConnection);
}

void ThreadedPlaybackAudioService::submit(const playback::PlaybackEvent& event)
{
    submitBatch(QVector<playback::PlaybackEvent> {event}, m_submissionGeneration.load(std::memory_order_acquire));
}

void ThreadedPlaybackAudioService::submitOff(const playback::PlaybackEvent& event)
{
    const quint64 generation = m_submissionGeneration.load(std::memory_order_acquire);
    QMetaObject::invokeMethod(m_worker.get(), [this, event, generation] {
        if (generation != m_worker->m_eventGeneration) return;
        m_worker->m_service->submitOff(event);
    }, Qt::QueuedConnection);
}

void ThreadedPlaybackAudioService::submitBatch(const QVector<playback::PlaybackEvent>& events)
{
    submitBatch(events, m_submissionGeneration.load(std::memory_order_acquire));
}

void ThreadedPlaybackAudioService::submitBatch(const QVector<playback::PlaybackEvent>& events, quint64 generation)
{
    if (events.isEmpty()) return;
    QMetaObject::invokeMethod(m_worker.get(), [this, events, generation] {
        if (generation != m_worker->m_eventGeneration) return;
        m_worker->m_service->submitBatch(events, generation);
    }, Qt::QueuedConnection);
}

} // namespace midi_play::audio
