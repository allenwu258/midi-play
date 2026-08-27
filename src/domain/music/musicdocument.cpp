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

qint64 MusicDocument::playbackTickToMicroseconds(Tick outputTick) const
{
    outputTick = std::max<Tick>(0, outputTick);
    const auto segments = playbackSegments();
    if (segments.isEmpty()) {
        return tickToMicroseconds(outputTick);
    }

    qint64 elapsedUs = 0;
    for (const auto& segment : segments) {
        const Tick length = std::max<Tick>(0, segment.sourceEnd - segment.sourceStart);
        if (outputTick < segment.outputStart + length) {
            const Tick sourceTick = segment.sourceStart + (outputTick - segment.outputStart);
            return elapsedUs + (tickToMicroseconds(sourceTick) - tickToMicroseconds(segment.sourceStart));
        }
        elapsedUs += tickToMicroseconds(segment.sourceEnd) - tickToMicroseconds(segment.sourceStart);
    }
    return elapsedUs;
}

QVector<PlaybackSegment> MusicDocument::playbackSegments() const
{
    QVector<PlaybackSegment> result;
    if (m_tracks.isEmpty() || m_tracks.front().measures.isEmpty()) {
        result.push_back({0, m_duration, 0});
        return result;
    }
    const auto& measures = m_tracks.front().measures;
    int segnoIndex = -1;
    int codaIndex = -1;
    for (int index = 0; index < measures.size(); ++index) {
        if (measures[index].segno && segnoIndex < 0) segnoIndex = index;
        if (measures[index].coda && codaIndex < 0) codaIndex = index;
    }

    int repeatStart = 0;
    int repeatPass = 0;
    int index = 0;
    Tick output = 0;
    bool jumpedDaCapo = false;
    bool jumpedDalSegno = false;
    bool codaArmed = false;

    // The guard makes malformed scores with self-referential jump markings
    // terminate deterministically instead of locking the playback thread.
    const int maxSteps = qMax(1024, measures.size() * 32);
    for (int step = 0; index >= 0 && index < measures.size() && step < maxSteps; ++step) {
        const auto& measure = measures[index];
        if (measure.repeatStart) {
            repeatStart = index;
            repeatPass = 0;
        }

        if (measure.endingNumber == 0 || measure.endingNumber == repeatPass + 1) {
            result.push_back({measure.start, measure.start + measure.duration, output});
            output += measure.duration;
        }

        if (measure.repeatEnd) {
            const int repeatCount = qMax(1, measure.repeatCount);
            if (repeatPass + 1 < repeatCount) {
                ++repeatPass;
                index = repeatStart;
                continue;
            }
            repeatStart = index + 1;
            repeatPass = 0;
        }

        if (codaArmed && measure.toCoda && codaIndex >= 0) {
            index = codaIndex;
            codaArmed = false;
            repeatStart = index;
            repeatPass = 0;
            continue;
        }
        if ((jumpedDaCapo || jumpedDalSegno) && measure.fine) {
            break;
        }
        if (!jumpedDaCapo && measure.daCapo) {
            jumpedDaCapo = true;
            codaArmed = true;
            index = 0;
            repeatStart = 0;
            repeatPass = 0;
            continue;
        }
        if (!jumpedDalSegno && measure.dalSegno && segnoIndex >= 0) {
            jumpedDalSegno = true;
            codaArmed = true;
            index = segnoIndex;
            repeatStart = index;
            repeatPass = 0;
            continue;
        }
        ++index;
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
