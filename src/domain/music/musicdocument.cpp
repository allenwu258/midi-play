#include "musicdocument.h"

#include <algorithm>
#include <cmath>

namespace midi_play::music {

int KeyContext::tonicPitchClass() const
{
    const int normalizedFifths = std::clamp(fifths, -7, 7);
    int tonic = (normalizedFifths * 7) % 12;
    if (tonic < 0) tonic += 12;
    if (mode.compare(QStringLiteral("minor"), Qt::CaseInsensitive) == 0) tonic = (tonic + 9) % 12;
    return tonic;
}

int KeyContext::degreeFor(const WrittenPitch& pitch) const
{
    static constexpr int majorScale[] = {0, 2, 4, 5, 7, 9, 11};
    static constexpr int minorScale[] = {0, 2, 3, 5, 7, 8, 10};
    const auto& scale = mode.compare(QStringLiteral("minor"), Qt::CaseInsensitive) == 0
        ? minorScale : majorScale;
    const int tonic = tonicPitchClass();
    int bestDegree = 1;
    int bestDistance = 12;
    for (int degree = 0; degree < 7; ++degree) {
        const int candidate = (tonic + scale[degree]) % 12;
        const int distance = std::min((pitch.midiPitch - candidate + 12) % 12,
                                      (candidate - pitch.midiPitch + 12) % 12);
        if (distance < bestDistance) {
            bestDistance = distance;
            bestDegree = degree + 1;
        }
    }
    return bestDegree;
}

RepeatList RepeatList::build(const QVector<Measure>& measures, Tick duration)
{
    RepeatList list;
    if (measures.isEmpty()) {
        list.m_segments.push_back({0, duration, 0, 0, -1});
        list.m_duration = duration;
        return list;
    }

    int segnoIndex = -1;
    int codaIndex = -1;
    for (int index = 0; index < measures.size(); ++index) {
        if (measures[index].segno && segnoIndex < 0) segnoIndex = index;
        if (measures[index].coda && codaIndex < 0) codaIndex = index;
    }

    RepeatJumpContext context;
    int index = 0;
    Tick output = 0;
    const int maxSteps = qMax(1024, measures.size() * 64);
    for (int step = 0; index >= 0 && index < measures.size() && step < maxSteps; ++step) {
        const auto& measure = measures[index];
        if (measure.repeatStart) {
            context.repeatStartIndex = index;
            context.repeatPass = 0;
        }

        const bool inEnding = measure.endingNumber > 0;
        const bool endingSelected = !inEnding || measure.endingNumber == context.repeatPass + 1;
        if (endingSelected) {
            list.m_segments.push_back({measure.start, measure.start + measure.duration,
                                       output, context.repeatPass, index});
            output += measure.duration;
        }

        if (measure.repeatEnd) {
            const int repeatCount = qMax(1, measure.repeatCount);
            if (context.repeatPass + 1 < repeatCount) {
                ++context.repeatPass;
                index = context.repeatStartIndex;
                continue;
            }
            context.repeatStartIndex = index + 1;
            context.repeatPass = 0;
        }

        if (context.codaArmed && measure.toCoda && codaIndex >= 0) {
            index = codaIndex;
            context.codaArmed = false;
            context.repeatStartIndex = index;
            context.repeatPass = 0;
            continue;
        }
        if ((context.daCapoTaken || context.dalSegnoTaken) && measure.fine) break;

        if (!context.daCapoTaken && measure.daCapo) {
            context.daCapoTaken = true;
            context.codaArmed = true;
            index = 0;
            context.repeatStartIndex = 0;
            context.repeatPass = 0;
            continue;
        }
        if (!context.dalSegnoTaken && measure.dalSegno && segnoIndex >= 0) {
            context.dalSegnoTaken = true;
            context.codaArmed = true;
            index = segnoIndex;
            context.repeatStartIndex = index;
            context.repeatPass = 0;
            continue;
        }
        ++index;
    }

    if (list.m_segments.isEmpty()) list.m_segments.push_back({0, duration, 0, 0, -1});
    const auto& last = list.m_segments.back();
    list.m_duration = last.outputStart + (last.sourceEnd - last.sourceStart);
    return list;
}

qint64 MusicDocument::tickToMicroseconds(Tick tick) const
{
    tick = std::clamp<Tick>(tick, 0, m_duration);
    rebuildTempoMap();
    if (m_tempoMap.isEmpty()) return static_cast<qint64>(std::llround(
        tick * 60'000'000.0 / (120.0 * kPpq)));
    const auto it = std::upper_bound(m_tempoMap.cbegin(), m_tempoMap.cend(), tick,
                                     [](Tick value, const TempoSegment& segment) {
                                         return value < segment.start;
                                     });
    const auto& segment = it == m_tempoMap.cbegin() ? m_tempoMap.front() : *std::prev(it);
    return segment.startUs + static_cast<qint64>(std::llround(
        (tick - segment.start) * 60'000'000.0 / (segment.bpm * kPpq)));
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

void MusicDocument::rebuildTempoMap() const
{
    if (!m_tempoMap.isEmpty()) return;
    QVector<TempoChange> changes = m_tempos;
    std::sort(changes.begin(), changes.end(), [](const auto& left, const auto& right) {
        if (left.tick != right.tick) return left.tick < right.tick;
        return left.sequence < right.sequence;
    });
    double bpm = 120.0;
    Tick cursor = 0;
    qint64 elapsedUs = 0;
    for (const auto& change : changes) {
        const Tick tick = std::clamp<Tick>(change.tick, 0, m_duration);
        if (tick < cursor) continue;
        elapsedUs += static_cast<qint64>(std::llround(
            (tick - cursor) * 60'000'000.0 / (bpm * kPpq)));
        bpm = change.bpm > 0.0 ? change.bpm : bpm;
        if (!m_tempoMap.isEmpty() && m_tempoMap.back().start == tick) {
            m_tempoMap.back().startUs = elapsedUs;
            m_tempoMap.back().bpm = bpm;
        } else {
            m_tempoMap.push_back({tick, m_duration, elapsedUs, bpm});
        }
        cursor = tick;
    }
    if (m_tempoMap.isEmpty() || m_tempoMap.front().start != 0) {
        m_tempoMap.push_front({0, m_duration, 0, 120.0});
    }
    for (int i = 0; i + 1 < m_tempoMap.size(); ++i) {
        m_tempoMap[i].end = m_tempoMap[i + 1].start;
    }
    m_tempoMap.back().end = m_duration;
}

void MusicDocument::rebuildMeasureGrid()
{
    m_measures.clear();
    for (const auto& track : m_tracks) {
        if (track.measures.size() > m_measures.size()) m_measures = track.measures;
    }
    if (m_measures.isEmpty() && m_duration > 0) {
        m_measures.push_back({1, 0, m_duration});
    }
}

QVector<PlaybackSegment> MusicDocument::playbackSegments() const
{
    if (m_measures.isEmpty()) return {{0, m_duration, 0, 0, -1}};
    return RepeatList::build(m_measures, m_duration).segments();
}

Tick MusicDocument::playbackDuration() const
{
    const auto segments = playbackSegments();
    if (segments.isEmpty()) return m_duration;
    return RepeatList::build(m_measures, m_duration).duration();
}

} // namespace midi_play::music
