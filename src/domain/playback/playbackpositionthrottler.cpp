#include "playbackpositionthrottler.h"

#include <chrono>

namespace midi_play::playback {

void PlaybackPositionThrottler::publish(qint64 positionUs, qint64 durationUs) noexcept
{
    // A single producer brackets each update with an odd/even sequence.
    // Sequential consistency prevents a consumer accepting a future payload
    // under the preceding sequence, including its new clock timestamp.
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
            // A producer update raced the read. Retry so a torn pair of
            // position/duration values can never be exposed to the UI.
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
