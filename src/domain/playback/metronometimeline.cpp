#include "metronometimeline.h"

#include <algorithm>
#include <cmath>

namespace midi_play::playback {
namespace {

using music::Tick;
// Bound source work and repeat expansion for malformed/huge documents.
constexpr int kMaximumBeats = 1'000'000;

struct SourceBeat {
    Tick tick = 0;
    int measureIndex = -1;
    int beatIndex = 0;
    bool downbeat = false;
};

template<class T>
QVector<T> canonicalChanges(QVector<T> changes)
{
    std::stable_sort(changes.begin(), changes.end(), [](const T& a, const T& b) {
        return a.tick < b.tick;
    });
    QVector<T> result;
    for (const auto& change : changes) {
        if (change.tick < 0) continue;
        if (!result.isEmpty() && result.back().tick == change.tick) result.back() = change;
        else result.push_back(change);
    }
    return result;
}

template<class T>
auto after(const QVector<T>& values, Tick tick)
{
    return std::upper_bound(values.cbegin(), values.cend(), tick,
        [](Tick value, const T& change) { return value < change.tick; });
}

long double denominatorTicks(const music::TimeSignatureChange& signature)
{
    // SMF bb is the count of notated 32nds per MIDI quarter, not always 8.
    return static_cast<long double>(music::MusicDocument::kPpq) * 32
        / (static_cast<long double>(signature.beatType) * signature.notated32nds);
}

bool buildSourceBeats(const music::MusicDocument& document, QVector<SourceBeat>& result)
{
    QVector<music::TimeSignatureChange> signatures;
    bool hasWrittenMeasures = false;
    for (const auto& track : document.tracks()) {
        signatures += track.timeSignatures;
        hasWrittenMeasures |= !track.measures.isEmpty();
    }
    signatures = canonicalChanges(std::move(signatures));
    if (signatures.isEmpty() || signatures.front().tick > 0) signatures.push_front({0, 4, 4});
    const auto units = canonicalChanges(document.metronomeUnits());
    for (const auto& signature : signatures) {
        if (signature.beats < 1 || signature.beats > 1024 || signature.beatType < 1
            || signature.beatType > 1024 || signature.notated32nds < 1
            || signature.notated32nds > 255 || signature.metronomeClocks < 0
            || signature.metronomeClocks > 255) return false;
        int sum = 0;
        for (int group : signature.beatGroups) {
            if (group < 1 || group > signature.beats) return false;
            sum += group;
        }
        if (!signature.beatGroups.isEmpty() && sum != signature.beats) return false;
    }
    for (const auto& unit : units) {
        if (!std::isfinite(unit.quarterNotes) || unit.quarterNotes < 1.0 / 256
            || unit.quarterNotes > 256) return false;
    }

    // Round each position from its musical anchor, never accumulate a rounded
    // interval (which would drift for high denominators or nonstandard SMF bb).
    auto emitMeasure = [&](Tick start, Tick end, int index, bool pickup) {
        Tick sliceStart = start;
        int beatIndex = 0;
        while (sliceStart < end) {
            const auto nextSignature = after(signatures, sliceStart);
            const auto& signature = *(nextSignature - 1);
            const auto nextUnit = after(units, sliceStart);
            Tick sliceEnd = end;
            if (nextSignature != signatures.cend()) sliceEnd = std::min(sliceEnd, nextSignature->tick);
            if (nextUnit != units.cend()) sliceEnd = std::min(sliceEnd, nextUnit->tick);
            const long double notated = denominatorTicks(signature);
            const long double nominal = notated * signature.beats;
            // A tempo/click-unit annotation splits evaluation, not musical
            // phase. Keep the bar (or actual meter-change) anchor across unit
            // slices, including a pickup whose first beat precedes tick zero.
            const Tick meterStart = std::max(start, signature.tick);
            const bool isPickup = pickup && meterStart == start && end - start < nominal;
            const long double anchor = isPickup ? end - nominal : meterStart;
            QVector<long double> pattern;
            long double period = 0;
            if (nextUnit == units.cbegin() && signature.metronomeClocks == 0
                && !signature.beatGroups.isEmpty()) {
                for (int group : signature.beatGroups) {
                    pattern.push_back(period);
                    period += group * notated;
                }
            } else {
                pattern.push_back(0);
                if (nextUnit != units.cbegin()) {
                    period = (nextUnit - 1)->quarterNotes * music::MusicDocument::kPpq;
                } else if (signature.metronomeClocks > 0) {
                    period = static_cast<long double>(music::MusicDocument::kPpq)
                        * signature.metronomeClocks / 24;
                } else {
                    period = notated * (signature.beats >= 6 && signature.beats % 3 == 0 ? 3 : 1);
                }
            }
            if (period < 1) return false;
            const auto firstCycle = static_cast<qint64>(std::max(0.0L,
                std::floor((sliceStart - anchor) / period)));
            for (qint64 cycle = firstCycle;; ++cycle) {
                const long double base = anchor + cycle * period;
                if (base >= static_cast<long double>(sliceEnd) + 0.5L) break;
                for (long double offset : pattern) {
                    const long double position = base + offset;
                    if (position < static_cast<long double>(sliceStart) - 0.5L
                        || position >= static_cast<long double>(sliceEnd) - 0.5L) continue;
                    const Tick tick = static_cast<Tick>(std::llround(position));
                    if (tick < sliceStart || tick >= sliceEnd) continue;
                    if (result.size() >= kMaximumBeats) return false;
                    result.push_back({tick, index, beatIndex++, tick == start && !isPickup});
                }
            }
            sliceStart = sliceEnd;
        }
        return true;
    };

    if (hasWrittenMeasures) {
        Tick previousEnd = 0;
        int index = 0;
        for (const auto& measure : document.measures()) {
            if (measure.start < previousEnd || measure.duration <= 0
                || measure.start > document.duration()
                || measure.duration > document.duration() - measure.start) return false;
            const Tick end = measure.start + measure.duration;
            if (!emitMeasure(measure.start, end, index, index == 0 && measure.implicit)) return false;
            previousEnd = end;
            if (++index > kMaximumBeats) return false;
        }
    } else {
        // A MIDI document measure is a placeholder, not a musical bar.
        QVector<Tick> boundaries {0, document.duration()};
        for (const auto& signature : signatures) boundaries.push_back(signature.tick);
        boundaries += document.sequenceStarts();
        std::sort(boundaries.begin(), boundaries.end());
        boundaries.erase(std::unique(boundaries.begin(), boundaries.end()), boundaries.end());
        int index = 0;
        for (int region = 0; region + 1 < boundaries.size(); ++region) {
            const Tick origin = boundaries[region];
            const Tick limit = std::min(boundaries[region + 1], document.duration());
            if (origin < 0 || origin >= limit) continue;
            const auto& signature = *(after(signatures, origin) - 1);
            const long double length = denominatorTicks(signature) * signature.beats;
            if (length < 1) return false;
            Tick start = origin;
            for (qint64 bar = 1; start < limit; ++bar) {
                const auto exactEnd = static_cast<long double>(origin) + bar * length;
                const Tick end = exactEnd >= limit ? limit : static_cast<Tick>(std::llround(exactEnd));
                if (end <= start || !emitMeasure(start, end, index++, false)) return false;
                if (index > kMaximumBeats) return false;
                start = end;
            }
        }
    }
    return true;
}

} // namespace

MetronomeTimeline::MetronomeTimeline(
    std::shared_ptr<const music::MusicDocument> document,
    std::shared_ptr<const music::PlaybackTimeline> timeline)
{
    if (!document || !timeline || !document->isValid()) {
        m_unavailableReason = QStringLiteral("请先加载乐曲");
        return;
    }
    if (!document->hasMusicalTimebase()) {
        m_unavailableReason = QStringLiteral("此 MIDI 使用 SMPTE 时间码，无法可靠推导音乐节拍");
        return;
    }
    QVector<SourceBeat> source;
    if (!buildSourceBeats(*document, source)) {
        m_unavailableReason = QStringLiteral("乐曲拍号无效或节拍数量超过安全上限");
        return;
    }
    quint64 occurrence = 0;
    for (const auto& segment : timeline->segments()) {
        auto beat = std::lower_bound(source.cbegin(), source.cend(), segment.sourceStart,
            [](const SourceBeat& value, Tick tick) { return value.tick < tick; });
        for (; beat != source.cend() && beat->tick < segment.sourceEnd; ++beat) {
            if (m_beats.size() >= kMaximumBeats) {
                m_beats.clear();
                m_unavailableReason = QStringLiteral("重复展开后的节拍数量超过安全上限");
                return;
            }
            const Tick output = segment.outputStart + beat->tick - segment.sourceStart;
            m_beats.push_back({timeline->outputTickToMicroseconds(output), occurrence++,
                beat->measureIndex, beat->beatIndex,
                beat->downbeat ? MetronomeAccent::Measure : MetronomeAccent::Beat});
        }
    }
    if (m_beats.isEmpty()) m_unavailableReason = QStringLiteral("乐曲没有可播放的节拍");
}

int MetronomeTimeline::lowerBound(qint64 timeUs) const
{
    return static_cast<int>(std::lower_bound(m_beats.cbegin(), m_beats.cend(), timeUs,
        [](const auto& beat, qint64 value) { return beat.timeUs < value; }) - m_beats.cbegin());
}

void MetronomeScheduler::seek(const MetronomeTimeline& timeline, qint64 timeUs)
{
    m_cursor = timeline.lowerBound(timeUs);
}

std::optional<MetronomeBeat> MetronomeScheduler::takeDue(
    const MetronomeTimeline& timeline, qint64 timeUs, double rate)
{
    const auto& beats = timeline.beats();
    const int end = static_cast<int>(std::upper_bound(beats.cbegin() + m_cursor,
        beats.cend(), timeUs, [](qint64 value, const MetronomeBeat& beat) {
            return value < beat.timeUs;
        }) - beats.cbegin());
    if (end <= m_cursor) return {};
    m_cursor = end;
    const auto& latest = beats[end - 1];
    constexpr qint64 kMaximumLateWallUs = 50'000;
    if (!std::isfinite(rate) || rate <= 0
        || static_cast<long double>(timeUs - latest.timeUs) / rate > kMaximumLateWallUs) return {};
    return latest;
}

} // namespace midi_play::playback
