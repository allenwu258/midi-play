#include "playbackpositionthrottler.h"

// 实现带序列一致性校验的位置采样发布和消费。
#include <chrono>

namespace midi_play::playback {

// 以奇数序列标记写入中、偶数序列标记写入完成，消费者据此进行一致性校验。
void PlaybackPositionThrottler::publish(qint64 positionUs, qint64 durationUs) noexcept
{
    m_sequence.fetch_add(1);
    m_positionUs.store(positionUs);
    m_durationUs.store(durationUs);
    m_sampledAtUs.store(std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
    m_sequence.fetch_add(1);
}

bool PlaybackPositionThrottler::takeLatest(Snapshot& snapshot) noexcept
{
    for (;;) {
        const quint64 before = m_sequence.load();
        if (before == 0 || before == m_consumedSequence) {
            return false;
        }

        if (before & 1U) continue;
        const qint64 positionUs = m_positionUs.load();
        const qint64 durationUs = m_durationUs.load();
        const qint64 sampledAtUs = m_sampledAtUs.load();
        const quint64 after = m_sequence.load();
        if (before != after) {
            // 生产者在读取期间更新了数据；重试，避免把不同采样的
            // position/duration 或时间戳组合后暴露给 UI。
            continue;
        }

        snapshot = {positionUs, durationUs, after / 2, sampledAtUs};
        m_consumedSequence = after;
        return true;
    }
}

void PlaybackPositionThrottler::reset() noexcept
{
    m_positionUs.store(0, std::memory_order_relaxed);
    m_durationUs.store(0, std::memory_order_relaxed);
    m_sampledAtUs.store(0, std::memory_order_relaxed);
    m_sequence.store(0, std::memory_order_release);
    m_consumedSequence = 0;
}

} // namespace midi_play::playback
