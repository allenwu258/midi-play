#include "playbackcontroller.h"

// 实现播放会话的线程生命周期、命令转发和位置节流。
#include "domain/settings/playersettings.h"

#include <QMetaObject>
#include <QThread>

namespace midi_play::playback {

// 初始化播放线程命名和高精度位置发布定时器；会话在 setDocument 时创建。
PlaybackController::PlaybackController(QObject* parent)
    : QObject(parent)
{
    m_playbackThread.setObjectName(QStringLiteral("PlaybackThread"));
    m_positionTimer.setInterval(settings::visualizationRefreshPeriod(m_positionPublishRate));
    m_positionTimer.setTimerType(Qt::PreciseTimer);
    connect(&m_positionTimer, &QChronoTimer::timeout, this, &PlaybackController::onPositionTimer);
}

PlaybackController::~PlaybackController()
{
    // 先停止位置定时器和会话，再退出线程，确保 queued 调用不再访问已释放会话。
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
    // 文档和音频服务必须同时有效，才能创建可播放会话。
    if (!document || !document->isValid() || !audioService) {
        if (error) *error = QStringLiteral("无法创建无效的播放会话");
        return false;
    }
    auto newSession = std::make_unique<PlaybackSession>(std::move(document), std::move(audioService));
    if (!newSession->setPlaybackRatePercent(m_playbackRatePercent)) {
        if (error) *error = QStringLiteral("此音频后端尚不支持播放变速");
        return false;
    }
    if (m_session) {
        // 替换旧会话前同步停止并等待旧线程退出，避免旧信号污染新歌曲。
        QMetaObject::invokeMethod(m_session.get(), "stop", Qt::BlockingQueuedConnection);
        m_playbackThread.quit();
        m_playbackThread.wait();
        m_session.reset();
    }
    m_positionTimer.stop();
    m_positionThrottler.reset();
    m_supportsPlaybackRate = newSession->supportsPlaybackRate();
    m_supportsMetronome = newSession->supportsMetronome();
    m_metronomeUnavailableReason = newSession->metronomeUnavailableReason();
    newSession->setMetronomeEnabled(m_metronomeEnabled);
    m_session = std::move(newSession);
    m_playbackThread.start();
    m_session->moveToThread(&m_playbackThread);
    connect(m_session.get(), &PlaybackSession::metronomeAvailabilityChanged, this,
            [this, source = m_session.get()](bool available, const QString& reason) {
                // 被替换会话的排队信号可能仍在 GUI 队列中，不能覆盖新歌曲的可用性。
                if (m_session.get() != source) return;
                m_supportsMetronome = available;
                m_metronomeUnavailableReason = reason;
                emit metronomeAvailabilityChanged(available, reason);
            });
    connect(m_session.get(), &PlaybackSession::stateChanged, this,
            [this](State state) {
                // 状态变化是传输边界；先刷新最新位置，避免 pause/stop 让 UI 落后一个节流周期。
                flushPositionUpdate();
                if (state == State::Playing) {
                    m_positionTimer.start();
                } else {
                    m_positionTimer.stop();
                }
                emit stateChanged(state);
            });
    // 位置采样从播放线程直接交接，在控制器定时器中合并发布；
    // 这样调度器的 2 ms 节奏不受 GUI 事件队列影响。
    connect(m_session.get(), &PlaybackSession::positionChanged, this,
            [this](qint64 position, qint64 duration) {
                m_positionThrottler.publish(position, duration);
            }, Qt::DirectConnection);
    connect(m_session.get(), &PlaybackSession::errorOccurred, this, &PlaybackController::errorOccurred);
    connect(m_session.get(), &PlaybackSession::soundFontLoadFinished,
            this, &PlaybackController::soundFontLoadFinished);
    return true;
}

bool PlaybackController::loadSoundFont(const QString& path, QString* error)
{
    // 使用阻塞排队调用保证返回时音源切换事务已经在播放线程完成。
    if (!m_session) return false;
    bool result = false;
    QString localError;
    bool available = false;
    QString reason;
    QMetaObject::invokeMethod(m_session.get(), [this, &result, &localError, &available, &reason, path] {
        result = m_session->loadSoundFont(path, &localError);
        available = m_session->supportsMetronome();
        reason = m_session->metronomeUnavailableReason();
    }, Qt::BlockingQueuedConnection);
    m_supportsMetronome = available;
    m_metronomeUnavailableReason = reason;
    if (error) *error = localError;
    return result;
}

void PlaybackController::loadSoundFontAsync(const QString& path)
{
    if (!m_session) {
        emit soundFontLoadFinished(false, QStringLiteral("播放会话不可用"));
        return;
    }
    // 异步调用由当前会话所属播放线程执行。
    QMetaObject::invokeMethod(m_session.get(), [this, path] {
        m_session->loadSoundFontAsync(path);
    }, Qt::QueuedConnection);
}

void PlaybackController::setPositionPublishRate(int refreshRate)
{
    // 设置项负责归一化频率；修改后立即刷新一次最新采样。
    const int normalizedRefreshRate = settings::normalizeVisualizationRefreshRate(refreshRate);
    if (m_positionPublishRate == normalizedRefreshRate) {
        return;
    }

    m_positionPublishRate = normalizedRefreshRate;
    m_positionTimer.setInterval(settings::visualizationRefreshPeriod(m_positionPublishRate));
    flushPositionUpdate();
}

bool PlaybackController::setPlaybackRatePercent(int percent)
{
    const int normalized = midi_play::settings::normalizePlaybackRatePercent(percent);
    if (normalized == m_playbackRatePercent) return true;
    if (m_session && !m_supportsPlaybackRate) {
        emit errorOccurred(QStringLiteral("此音频后端尚不支持播放变速"));
        return false;
    }
    m_playbackRatePercent = normalized;
    if (!m_session) return true;

    // 速率修改采用排队调用，不让 GUI 被音源 I/O 阻塞；调用序列与
    // play/pause/seek 保持一致，并捕获稳定的 session 指针。
    auto* session = m_session.get();
    QMetaObject::invokeMethod(session, [session, normalized] {
        session->setPlaybackRatePercent(normalized);
    }, Qt::QueuedConnection);
    return true;
}

void PlaybackController::setMetronomeEnabled(bool enabled)
{
    // 即使当前歌曲或后端不可用，也保留用户偏好；实际可用性单独报告。
    m_metronomeEnabled = enabled;
    if (!m_session) return;
    auto* session = m_session.get();
    QMetaObject::invokeMethod(session, [session, enabled] {
        session->setMetronomeEnabled(enabled);
    }, Qt::QueuedConnection);
}

// 传输命令全部异步投递到播放线程，避免 UI 线程直接操作音频后端。
void PlaybackController::play() { if (m_session) QMetaObject::invokeMethod(m_session.get(), "play", Qt::QueuedConnection); }
void PlaybackController::pause() { if (m_session) QMetaObject::invokeMethod(m_session.get(), "pause", Qt::QueuedConnection); }
void PlaybackController::stop() { if (m_session) QMetaObject::invokeMethod(m_session.get(), "stop", Qt::QueuedConnection); }
void PlaybackController::seek(qint64 microseconds)
{
    if (!m_session) {
        return;
    }

    // seek 同时修改定时器、播放头、调度器和音频状态；必须等待会话完成，
    // 避免 UI 释放滑块时与旧位置通知竞争。
    if (QThread::currentThread() == m_session->thread()) {
        m_session->seek(microseconds);
        // 当前已在播放线程，节流器由控制器线程消费，因此只排队请求控制器刷新。
        QMetaObject::invokeMethod(this, [this] {
            flushPositionUpdate();
        }, Qt::QueuedConnection);
        return;
    }
    QMetaObject::invokeMethod(m_session.get(), "seek", Qt::BlockingQueuedConnection,
                              Q_ARG(qint64, microseconds));
    // 阻塞 seek 返回表示事务已完成，立即发布精确端点，不等待下一个刷新周期。
    flushPositionUpdate();
}

void PlaybackController::onPositionTimer()
{
    flushPositionUpdate();
}

void PlaybackController::flushPositionUpdate()
{
    // 没有新采样时保持 UI 原值；有新采样时一次性发送最新状态。
    PlaybackPositionThrottler::Snapshot snapshot;
    if (!m_positionThrottler.takeLatest(snapshot)) {
        return;
    }
    emit positionChanged(snapshot.positionUs, snapshot.durationUs, snapshot.sampledAtUs);
}

} // namespace midi_play::playback
