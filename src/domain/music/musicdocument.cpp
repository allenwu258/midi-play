#include "musicdocument.h"

#include <algorithm>

namespace midi_play::music {

qint64 MusicDocument::tickToMicroseconds(Tick tick) const
{
    tick = std::clamp<Tick>(tick, 0, m_duration);
    if (m_tempos.isEmpty()) {
        return tick * 60'000'000LL / (120 * kPpq);
    }

    qint64 result = 0;
    Tick cursor = 0;
    double bpm = 120.0;
    for (const TempoChange& change : m_tempos) {
        if (change.tick > tick) {
            break;
        }
        result += static_cast<qint64>((change.tick - cursor) * 60'000'000.0 / (bpm * kPpq));
        cursor = change.tick;
        bpm = change.bpm > 0.0 ? change.bpm : bpm;
    }
    result += static_cast<qint64>((tick - cursor) * 60'000'000.0 / (bpm * kPpq));
    return result;
}

Tick MusicDocument::microsecondsToTick(qint64 microseconds) const
{
    if (microseconds <= 0) {
        return 0;
    }
    Tick low = 0;
    Tick high = m_duration;
    while (low < high) {
        const Tick mid = low + (high - low) / 2;
        if (tickToMicroseconds(mid) < microseconds) {
            low = mid + 1;
        } else {
            high = mid;
        }
    }
    return low;
}

} // namespace midi_play::music
