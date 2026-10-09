#pragma once

// 播放线程到 UI 线程的原子最新位置采样交接器。
#include <QtGlobal>

#include <atomic>

namespace midi_play::playback {

// 播放线程到应用线程的线程安全“最新值覆盖”交接器。
// 生产者只发布最新传输采样，不直接投递 GUI 任务；消费者应固定在一个线程。
class PlaybackPositionThrottler final {
public:
    // 一次完整的位置采样及其发布序号。
    struct Snapshot {
        qint64 positionUs = 0;
        qint64 durationUs = 0;
        quint64 sequence = 0;
        qint64 sampledAtUs = 0;
    };

    // 发布位置和时长，序列号用于防止消费者读到撕裂数据。
    void publish(qint64 positionUs, qint64 durationUs) noexcept;
    // 读取最近一次完整采样；没有新采样时返回 false。
    bool takeLatest(Snapshot& snapshot) noexcept;
    // 丢弃已发布采样并恢复初始状态。
    void reset() noexcept;

private:
    std::atomic<qint64> m_positionUs {0};
    std::atomic<qint64> m_durationUs {0};
    std::atomic<qint64> m_sampledAtUs {0};
    std::atomic<quint64> m_sequence {0};
    quint64 m_consumedSequence = 0;
};

} // namespace midi_play::playback
