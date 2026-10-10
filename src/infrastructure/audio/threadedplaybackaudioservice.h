#pragma once

// 为任意播放音频后端提供串行工作线程、非阻塞事件提交与代次过滤。
#include "domain/playback/iplaybackaudioservice.h"

#include <QThread>
#include <atomic>
#include <memory>

namespace midi_play::audio {

// 控制/加载调用同步等待 Worker，音符和点击通过队列异步执行。
// 阻塞接口须从 Worker 线程之外调用；实时调度读取缓存能力和原子时钟快照。
class ThreadedPlaybackAudioService final : public playback::IPlaybackAudioService {
public:
    // 接管非空后端，在线程启动前取得能力与时钟快照。
    explicit ThreadedPlaybackAudioService(std::unique_ptr<playback::IPlaybackAudioService> service);
    // 退出并等待工作线程，再由成员析构释放 Worker 和后端。
    ~ThreadedPlaybackAudioService() override;

    // 在 Worker 中加载音源，等待结束并回传错误。
    bool loadSoundFont(const QString& path, QString* error) override;
    // 同步串行配置通道与轨道资源。
    bool configureTrack(const QString& trackId, int channel, int program, QString* error) override;
    // 复制轨道数据交给 Worker，在后端注册结束后返回。
    bool addTrack(const playback::PlaybackData& data, QString* error) override;
    // 同步执行后端启动，供会话随后开始计时。
    bool start() override;
    // 同步执行后端暂停。
    bool pause() override;
    // 同步执行后端停止。
    bool stop() override;
    // 同步定位后端，返回后可进行状态恢复。
    bool seek(qint64 positionUs) override;
    // 同步设置后端传输位置，不与事件提交并行调用后端。
    bool setTransportPosition(qint64 positionUs) override;
    // 只读取共享原子快照；无快照时返回 -1，不跨线程调用后端。
    qint64 clockPositionUs() const override;
    // 返回有效时钟能力；无安全快照的设备时钟会回退软件时钟。
    playback::PlaybackClockSource clockSource() const override;
    // 查询构造时缓存的定时事件能力。
    bool supportsTimedEvents() const override;
    // 查询构造时缓存的单音符表达能力。
    bool supportsPerNoteExpression() const override;
    // 查询构造时缓存的节拍器契约，资源准备状态另行检查。
    bool supportsMetronome() const override;
    // 在 Worker 非实时加载路径准备点击资源并等待结果。
    bool prepareMetronome(QString* error) override;
    // 按值返回有效能力快照，不等待工作线程。
    playback::PlaybackBackendCapabilities capabilities() const override;
    // 同步清理后端当前音符/事件；代次变更由会话单独发起。
    bool flush() override;
    // 发布新提交代次，再同步更新 Worker 和后端代次。
    void setEventGeneration(quint64 generation) override;
    // 将单事件转为当前代次的异步批次。
    void submit(const playback::PlaybackEvent& event) override;
    // 排入释放事件，Worker 执行前核对代次。
    void submitOff(const playback::PlaybackEvent& event) override;
    // 使用当前提交代次异步发送批次。
    void submitBatch(const QVector<playback::PlaybackEvent>& events) override;
    // 使用指定代次异步发送批次，过期批次由 Worker 丢弃。
    void submitBatch(const QVector<playback::PlaybackEvent>& events, quint64 generation) override;
    // 点击另带取消轮次和 50 ms 截止时间，避免阻塞恢复后补发旧点击。
    void submitMetronomeClick(bool accent, quint64 generation) override;
    // 立即发布点击取消轮次，再异步停止点击，不等待音源加载完成。
    void stopMetronome() override;

private:
    // Worker 唯一持有后端，可变后端状态只在其队列调用中访问。
    class Worker;
    // 构造时取得的不可变共享对象，供调用线程无需等待地查询时钟和能力。
    const std::shared_ptr<const playback::PlaybackClockSnapshot> m_clockSnapshot;
    const playback::PlaybackBackendCapabilities m_capabilities;
    QThread m_thread; // 包装对象负责启动、退出和等待，后端调用只在其事件循环内串行执行。
    std::unique_ptr<Worker> m_worker; // 线程退出后释放；析构前调用方必须停止发起新任务。
    // 传输代次区分歌曲/seek 边界；点击轮次支持独立取消，不使歌曲事件失效。
    std::atomic<quint64> m_submissionGeneration {0};
    std::atomic<quint64> m_metronomeEpoch {0};
};

} // namespace midi_play::audio
