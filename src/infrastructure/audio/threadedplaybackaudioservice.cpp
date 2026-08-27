#include "threadedplaybackaudioservice.h"

#include <QMetaObject>
#include <QPointer>

namespace midi_play::audio {

class ThreadedPlaybackAudioService::Worker final : public QObject {
public:
    explicit Worker(std::unique_ptr<playback::IPlaybackAudioService> service)
        : m_service(std::move(service)) {}

    std::unique_ptr<playback::IPlaybackAudioService> m_service;
};

ThreadedPlaybackAudioService::ThreadedPlaybackAudioService(std::unique_ptr<playback::IPlaybackAudioService> service)
    : m_service(std::move(service)), m_worker(std::make_unique<Worker>(std::move(m_service)))
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

bool ThreadedPlaybackAudioService::flush()
{
    bool result = false;
    QMetaObject::invokeMethod(m_worker.get(), [this, &result] { result = m_worker->m_service->flush(); }, Qt::BlockingQueuedConnection);
    return result;
}

void ThreadedPlaybackAudioService::submit(const playback::PlaybackEvent& event)
{
    QMetaObject::invokeMethod(m_worker.get(), [this, event] { m_worker->m_service->submit(event); }, Qt::QueuedConnection);
}

void ThreadedPlaybackAudioService::submitOff(const playback::PlaybackEvent& event)
{
    QMetaObject::invokeMethod(m_worker.get(), [this, event] { m_worker->m_service->submitOff(event); }, Qt::QueuedConnection);
}

} // namespace midi_play::audio
