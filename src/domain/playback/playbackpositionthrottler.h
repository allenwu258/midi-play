#pragma once

#include <QtGlobal>

#include <atomic>

namespace midi_play::playback {

// Thread-safe latest-value-wins handoff between the playback thread and the
// application thread. The producer never queues GUI work; it only publishes
// the newest transport sample. Consumption is expected on one thread.
class PlaybackPositionThrottler final {
public:
    struct Snapshot {
        qint64 positionUs = 0;
        qint64 durationUs = 0;
        quint64 sequence = 0;
    };

    void publish(qint64 positionUs, qint64 durationUs) noexcept;
    bool takeLatest(Snapshot& snapshot) noexcept;
    void reset() noexcept;

private:
    std::atomic<qint64> m_positionUs {0};
    std::atomic<qint64> m_durationUs {0};
    std::atomic<quint64> m_sequence {0};
    quint64 m_consumedSequence = 0;
};

} // namespace midi_play::playback
