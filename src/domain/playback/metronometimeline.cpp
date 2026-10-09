#include "metronometimeline.h"
#include "domain/music/musicrhythmgrid.h"

// 将共享节奏网格投影到反复展开后的播放时间线，并提供点击调度。
#include <algorithm>
#include <cmath>

namespace midi_play::playback {
namespace {

using music::Tick;
constexpr int kMaximumBeats = 1'000'000;

} // namespace

// 校验文档和节奏网格后，把每个源节拍映射到反复展开后的输出微秒时间。
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
        // 当前段只投影其源区间内的点击，避免重复段之间交叉复制。
        auto beat = std::lower_bound(source.cbegin(), source.cend(), segment.sourceStart,
            [](const music::RhythmPoint& value, Tick tick) { return value.tick < tick; });
        for (; beat != source.cend() && beat->tick < segment.sourceEnd; ++beat) {
            if (m_beats.size() >= kMaximumBeats) {
                // 反复标记异常时限制内存增长，并把节拍器标记为不可用。
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

// 使用二分查找定位节拍器游标，要求 m_beats 已按时间排序。
int MetronomeTimeline::lowerBound(qint64 timeUs) const
{
    return static_cast<int>(std::lower_bound(m_beats.cbegin(), m_beats.cend(), timeUs,
        [](const auto& beat, qint64 value) { return beat.timeUs < value; }) - m_beats.cbegin());
}

// 跳转时丢弃目标时间之前的点击，下一次 takeDue 从当前位置开始。
void MetronomeScheduler::seek(const MetronomeTimeline& timeline, qint64 timeUs)
{
    m_cursor = timeline.lowerBound(timeUs);
}

// 先消费所有已到期点击，再只返回最新一个；过晚的点击不再补发。
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
    // 把音乐时间的迟到量换算为墙上时间，避免恢复播放时连续补齐历史点击。
    if (!std::isfinite(rate) || rate <= 0
        || static_cast<long double>(timeUs - latest.timeUs) / rate > kMaximumLateWallUs) return {};
    return latest;
}

} // namespace midi_play::playback
