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

QVector<PlaybackSegment> MusicDocument::playbackSegments() const
{
    QVector<PlaybackSegment> result;
    if (m_tracks.isEmpty() || m_tracks.front().measures.isEmpty()) {
        result.push_back({0, m_duration, 0});
        return result;
    }
    const auto& measures = m_tracks.front().measures;
    int repeatStart = 0;
    Tick output = 0;
    for (int i = 0; i < measures.size(); ++i) {
        if (measures[i].repeatStart) repeatStart = i;
        if (!measures[i].repeatEnd && i + 1 < measures.size()) continue;
        const int count = measures[i].repeatEnd ? qMax(1, measures[i].repeatCount) : 1;
        const Tick sourceStart = measures[repeatStart].start;
        const Tick sourceEnd = measures[i].start + measures[i].duration;
        for (int pass = 0; pass < count; ++pass) {
            for (int measureIndex = repeatStart; measureIndex <= i; ++measureIndex) {
                const auto& measure = measures[measureIndex];
                if (measure.endingNumber > 0 && measure.endingNumber != pass + 1) continue;
                result.push_back({measure.start, measure.start + measure.duration, output});
                output += measure.duration;
            }
        }
        repeatStart = i + 1;
    }
    if (result.isEmpty()) result.push_back({0, m_duration, 0});
    return result;
}

Tick MusicDocument::playbackDuration() const
{
    const auto segments = playbackSegments();
    if (segments.isEmpty()) return m_duration;
    const auto& last = segments.back();
    return last.outputStart + (last.sourceEnd - last.sourceStart);
}

} // namespace midi_play::music
