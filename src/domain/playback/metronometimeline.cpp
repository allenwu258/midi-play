#include "metronometimeline.h"
#include "domain/music/musicrhythmgrid.h"

#include <algorithm>
#include <cmath>

namespace midi_play::playback {
namespace {

using music::Tick;
constexpr int kMaximumBeats = 1'000'000;

} // namespace

MetronomeTimeline::MetronomeTimeline(
    std::shared_ptr<const music::MusicDocument> document,
    std::shared_ptr<const music::PlaybackTimeline> timeline)
{
    if (!document || !timeline || !document->isValid()) {
        m_unavailableReason = QStringLiteral("请先加载乐曲");
        return;
    }
    const music::MusicRhythmGrid grid(*document);
    if (!grid.available()) {
        m_unavailableReason = grid.unavailableReason();
        return;
    }
    const auto& source = grid.clicks();
    quint64 occurrence = 0;
    for (const auto& segment : timeline->segments()) {
        auto beat = std::lower_bound(source.cbegin(), source.cend(), segment.sourceStart,
            [](const music::RhythmPoint& value, Tick tick) { return value.tick < tick; });
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
