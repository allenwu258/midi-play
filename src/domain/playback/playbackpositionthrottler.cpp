#include "playbackpositionthrottler.h"

namespace midi_play::playback {

void PlaybackPositionThrottler::publish(qint64 positionUs, qint64 durationUs) noexcept
{
    // Publish the payload before the release sequence update. The consumer
    // uses the sequence as a consistency marker while reading the payload.
    m_positionUs.store(positionUs, std::memory_order_relaxed);
    m_durationUs.store(durationUs, std::memory_order_relaxed);
    m_sequence.fetch_add(1, std::memory_order_release);
}

bool PlaybackPositionThrottler::takeLatest(Snapshot& snapshot) noexcept
{
    for (;;) {
        const quint64 before = m_sequence.load(std::memory_order_acquire);
        if (before == 0 || before == m_consumedSequence) {
            return false;
        }

        const qint64 positionUs = m_positionUs.load(std::memory_order_relaxed);
        const qint64 durationUs = m_durationUs.load(std::memory_order_relaxed);
        const quint64 after = m_sequence.load(std::memory_order_acquire);
        if (before != after) {
            // A producer update raced the read. Retry so a torn pair of
            // position/duration values can never be exposed to the UI.
            continue;
        }

        snapshot = {positionUs, durationUs, after};
        m_consumedSequence = after;
        return true;
    }
}

void PlaybackPositionThrottler::reset() noexcept
{
    m_positionUs.store(0, std::memory_order_relaxed);
    m_durationUs.store(0, std::memory_order_relaxed);
    m_sequence.store(0, std::memory_order_release);
    m_consumedSequence = 0;
}

} // namespace midi_play::playback
