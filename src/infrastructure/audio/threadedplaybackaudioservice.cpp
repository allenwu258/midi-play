#include "threadedplaybackaudioservice.h"

// 实现音频 Worker 的同步控制、异步事件队列和无阻塞时钟查询。
#include <QMetaObject>
#include <QPointer>
#include <chrono>

namespace midi_play::audio {
namespace {

// 装饰后端的设备时钟只有具备原子快照时才能安全跨线程消费。
playback::PlaybackBackendCapabilities effectiveCapabilities(
    playback::PlaybackBackendCapabilities capabilities,
    const std::shared_ptr<const playback::PlaybackClockSnapshot>& clockSnapshot)
{
    // 没有独立快照时明确回退软件时钟，避免调用线程直接访问 Worker 的设备状态。
    if (capabilities.usesAudioClock() && !clockSnapshot) {
        capabilities.clockSource = playback::PlaybackClockSource::SoftwareMonotonic;
    }
    return capabilities;
}

} // namespace

// 队列调用的接收对象，独占后端；eventGeneration 仅由工作线程读写。
class ThreadedPlaybackAudioService::Worker final : public QObject {
public:
    // 接管后端，初始代次为零。
    explicit Worker(std::unique_ptr<playback::IPlaybackAudioService> service)
        : m_service(std::move(service)) {}

    std::unique_ptr<playback::IPlaybackAudioService> m_service;
    quint64 m_eventGeneration = 0;
};

// 在后端被交给 Worker 前缓存跨线程可读信息，再启动 Qt 事件循环。
ThreadedPlaybackAudioService::ThreadedPlaybackAudioService(std::unique_ptr<playback::IPlaybackAudioService> service)
    : m_clockSnapshot(service ? service->clockSnapshot() : nullptr)
    , m_capabilities(effectiveCapabilities(
          service ? service->capabilities() : playback::PlaybackBackendCapabilities {},
          m_clockSnapshot))
    , m_worker(std::make_unique<Worker>(std::move(service)))
{
    m_worker->moveToThread(&m_thread);
    m_thread.start();
}

// 等待工作线程退出，避免服务成员释放时仍有正在执行的队列调用。
ThreadedPlaybackAudioService::~ThreadedPlaybackAudioService()
{
    m_thread.quit();
    m_thread.wait();
}

// 加载可能涉及磁盘 I/O；同步等待结果供调用方决定是否继续启动会话。
bool ThreadedPlaybackAudioService::loadSoundFont(const QString& path, QString* error)
{
    bool result = false;
    QString localError;
    // BlockingQueuedConnection 保证 lambda 完成后局部结果和错误仍在有效生命周期内。
    QMetaObject::invokeMethod(m_worker.get(), [this, &result, &localError, path] {
        result = m_worker->m_service->loadSoundFont(path, &localError);
    }, Qt::BlockingQueuedConnection);
    if (error) *error = localError;
    return result;
}

// 与其他后端控制串行执行，并将 Worker 产生的错误复制回调用线程。
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

// lambda 按值持有轨道数据，不依赖调用方对象在 Worker 中的可写访问。
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

// 控制操作同步完成，调用方可据返回值确定传输状态。
bool ThreadedPlaybackAudioService::start()
{
    bool result = false;
    QMetaObject::invokeMethod(m_worker.get(), [this, &result] { result = m_worker->m_service->start(); }, Qt::BlockingQueuedConnection);
    return result;
}

// 暂停在 Worker 队列中执行，不直接并发操作后端。
bool ThreadedPlaybackAudioService::pause()
{
    bool result = false;
    QMetaObject::invokeMethod(m_worker.get(), [this, &result] { result = m_worker->m_service->pause(); }, Qt::BlockingQueuedConnection);
    return result;
}

// 停止完成后返回，代次更新仍由会话在传输边界发起。
bool ThreadedPlaybackAudioService::stop()
{
    bool result = false;
    QMetaObject::invokeMethod(m_worker.get(), [this, &result] { result = m_worker->m_service->stop(); }, Qt::BlockingQueuedConnection);
    return result;
}

// 在后端定位完成后返回，供会话按目标位置重建状态。
bool ThreadedPlaybackAudioService::seek(qint64 positionUs)
{
    bool result = false;
    QMetaObject::invokeMethod(m_worker.get(), [this, &result, positionUs] { result = m_worker->m_service->seek(positionUs); }, Qt::BlockingQueuedConnection);
    return result;
}

// 将当前位置同步传给后端的传输契约。
bool ThreadedPlaybackAudioService::setTransportPosition(qint64 positionUs)
{
    bool result = false;
    QMetaObject::invokeMethod(m_worker.get(), [this, &result, positionUs] {
        result = m_worker->m_service->setTransportPosition(positionUs);
    }, Qt::BlockingQueuedConnection);
    return result;
}

// 调度热路径只做原子读取，不进入 Worker 队列也不等待加载操作。
qint64 ThreadedPlaybackAudioService::clockPositionUs() const
{
    return m_clockSnapshot ? m_clockSnapshot->positionUs() : -1;
}

// 以下能力查询均使用构造期缓存，不在每个调度 tick 跨线程访问后端。
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

bool ThreadedPlaybackAudioService::supportsMetronome() const
{
    return m_capabilities.metronome;
}

// 点击资源准备属于非实时操作，沿用加载接口的同步错误传播方式。
bool ThreadedPlaybackAudioService::prepareMetronome(QString* error)
{
    bool result = false;
    QString localError;
    QMetaObject::invokeMethod(m_worker.get(), [this, &result, &localError] {
        result = m_worker->m_service->prepareMetronome(&localError);
    }, Qt::BlockingQueuedConnection);
    if (error) *error = localError;
    return result;
}

// 返回经过时钟安全回退处理的完整能力快照。
playback::PlaybackBackendCapabilities ThreadedPlaybackAudioService::capabilities() const
{
    return m_capabilities;
}

// 同步冲刷后端；本调用不自行改变提交代次。
bool ThreadedPlaybackAudioService::flush()
{
    bool result = false;
    QMetaObject::invokeMethod(m_worker.get(), [this, &result] { result = m_worker->m_service->flush(); }, Qt::BlockingQueuedConnection);
    return result;
}

// 原子值对提交线程可见，Worker 的代次随后在同步屏障内更新。
void ThreadedPlaybackAudioService::setEventGeneration(quint64 generation)
{
    m_submissionGeneration.store(generation, std::memory_order_release);
    QMetaObject::invokeMethod(m_worker.get(), [this, generation] {
        m_worker->m_eventGeneration = generation;
        m_worker->m_service->setEventGeneration(generation);
    }, Qt::BlockingQueuedConnection);
}

// 单事件复用批次提交路径，保持统一的队列和代次校验。
void ThreadedPlaybackAudioService::submit(const playback::PlaybackEvent& event)
{
    submitBatch(QVector<playback::PlaybackEvent> {event}, m_submissionGeneration.load(std::memory_order_acquire));
}

// 释放事件按值捕获；执行时过期代次不得影响新传输中的同音高音符。
void ThreadedPlaybackAudioService::submitOff(const playback::PlaybackEvent& event)
{
    const quint64 generation = m_submissionGeneration.load(std::memory_order_acquire);
    QMetaObject::invokeMethod(m_worker.get(), [this, event, generation] {
        if (generation != m_worker->m_eventGeneration) return;
        m_worker->m_service->submitOff(event);
    }, Qt::QueuedConnection);
}

// 无显式代次的调用使用提交时发布的当前代次。
void ThreadedPlaybackAudioService::submitBatch(const QVector<playback::PlaybackEvent>& events)
{
    submitBatch(events, m_submissionGeneration.load(std::memory_order_acquire));
}

// 一次排队提交整个批次，减少事件逐条跨线程调度的开销。
void ThreadedPlaybackAudioService::submitBatch(const QVector<playback::PlaybackEvent>& events, quint64 generation)
{
    if (events.isEmpty()) return;
    QMetaObject::invokeMethod(m_worker.get(), [this, events, generation] {
        if (generation != m_worker->m_eventGeneration) return;
        m_worker->m_service->submitBatch(events, generation);
    }, Qt::QueuedConnection);
}

// 点击只在原取消轮次、传输代次和墙钟截止时间均有效时执行。
void ThreadedPlaybackAudioService::submitMetronomeClick(bool accent, quint64 generation)
{
    const quint64 epoch = m_metronomeEpoch.load(std::memory_order_acquire);
    // 音源加载可能阻塞 Worker；过期点击直接丢弃，防止恢复后密集补发。
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(50);
    QMetaObject::invokeMethod(m_worker.get(), [this, accent, generation, epoch, deadline] {
        if (epoch != m_metronomeEpoch.load(std::memory_order_acquire)
            || generation != m_submissionGeneration.load(std::memory_order_acquire)
            || generation != m_worker->m_eventGeneration
            || std::chrono::steady_clock::now() > deadline) return;
        m_worker->m_service->submitMetronomeClick(accent, generation);
    }, Qt::QueuedConnection);
}

// 点击取消不改变 MIDI 代次，允许只关闭节拍器而继续歌曲播放。
void ThreadedPlaybackAudioService::stopMetronome()
{
    // 先发布取消，使排队点击即刻失效；后端停止仍在 Worker 中异步串行执行。
    m_metronomeEpoch.fetch_add(1, std::memory_order_acq_rel);
    QMetaObject::invokeMethod(m_worker.get(), [this] {
        m_worker->m_service->stopMetronome();
    }, Qt::QueuedConnection);
}

} // namespace midi_play::audio
