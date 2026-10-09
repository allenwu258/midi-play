#include "playbackcontext.h"

// 将音乐域动态信息投影为可按输出微秒查询的播放上下文。
#include <algorithm>
#include <cmath>

namespace midi_play::playback {

// 构造一次性的输出时间上下文：源 tick 通过每个 PlaybackSegment 投影到
// 展开后的微秒时间，避免播放过程中重复扫描音乐域结构。
PlaybackContext::PlaybackContext(std::shared_ptr<const music::MusicDocument> document,
                                 std::shared_ptr<const music::PlaybackTimeline> timeline)
{
    if (!document || !timeline) return;
    const auto& segments = timeline->segments();
    for (const auto& segment : segments) {
        // 同一个源 tempo 在重复段中分别生成输出时间，后插入的值覆盖同 tick 项。
        for (const auto& tempo : document->tempos()) {
            if (tempo.tick < segment.sourceStart || tempo.tick >= segment.sourceEnd) continue;
            const music::Tick outputTick = segment.outputStart + (tempo.tick - segment.sourceStart);
            m_tempos.insert(timeline->outputTickToMicroseconds(outputTick), tempo.bpm);
        }
    }
    for (const auto& track : document->tracks()) {
        auto& points = m_dynamics[track.id];
        auto& hairpins = m_hairpins[track.id];

        QVector<const music::DynamicChange*> sortedDynamics;
        sortedDynamics.reserve(track.dynamics.size());
        for (const auto& dynamic : track.dynamics) sortedDynamics.push_back(&dynamic);
        // 保持同 tick 动态的导入顺序，保证结果可重现。
        std::stable_sort(sortedDynamics.begin(), sortedDynamics.end(), [](const auto* left, const auto* right) {
            return left->tick < right->tick;
        });

        QVector<const music::NoteEvent*> sortedVelocityNotes;
        sortedVelocityNotes.reserve(track.notes.size());
        for (const auto& note : track.notes) {
            if (note.velocity != 90) sortedVelocityNotes.push_back(&note);
        }
        // 非默认力度音符作为离散动态点，避免把所有普通音符写入上下文。
        std::stable_sort(sortedVelocityNotes.begin(), sortedVelocityNotes.end(), [](const auto* left, const auto* right) {
            return left->start < right->start;
        });

        for (const auto& segment : segments) {
            // projectTick 是当前段的局部源 tick 到输出微秒的统一映射。
            const auto projectTick = [&segment, &timeline](music::Tick sourceTick) {
                const music::Tick outputTick = segment.outputStart
                                             + (sourceTick - segment.sourceStart);
                return timeline->outputTickToMicroseconds(outputTick);
            };

            auto dynamicIt = std::lower_bound(
                sortedDynamics.cbegin(), sortedDynamics.cend(), segment.sourceStart,
                [](const auto* dynamic, music::Tick tick) { return dynamic->tick < tick; });
            for (; dynamicIt != sortedDynamics.cend() && (*dynamicIt)->tick < segment.sourceEnd;
                 ++dynamicIt) {
                points.push_back({projectTick((*dynamicIt)->tick), (*dynamicIt)->velocity});
            }

            auto noteIt = std::lower_bound(
                sortedVelocityNotes.cbegin(), sortedVelocityNotes.cend(), segment.sourceStart,
                [](const auto* note, music::Tick tick) { return note->start < tick; });
            for (; noteIt != sortedVelocityNotes.cend() && (*noteIt)->start < segment.sourceEnd;
                 ++noteIt) {
                points.push_back({projectTick((*noteIt)->start), (*noteIt)->velocity});
            }

            for (const auto& hairpin : track.hairpins) {
                const music::Tick sourceLength = hairpin.end - hairpin.start;
                if (sourceLength <= 0) continue;

                // 一个 hairpin 可能跨越反复段边界，因此按段裁剪并保留全局进度。
                const music::Tick overlapStart = std::max(hairpin.start, segment.sourceStart);
                const music::Tick overlapEnd = std::min(hairpin.end, segment.sourceEnd);
                if (overlapEnd <= overlapStart) continue;

                const double startProgress = static_cast<double>(overlapStart - hairpin.start)
                                           / static_cast<double>(sourceLength);
                const double endProgress = static_cast<double>(overlapEnd - hairpin.start)
                                         / static_cast<double>(sourceLength);
                const qint64 startUs = projectTick(overlapStart);
                const qint64 endUs = projectTick(overlapEnd);
                if (endUs > startUs) {
                    hairpins.push_back({startUs, endUs, startProgress, endProgress,
                                        hairpin.crescendo, overlapEnd == hairpin.end});
                }
            }
        }
        std::stable_sort(points.begin(), points.end(), [](const auto& left, const auto& right) {
            return left.timestampUs < right.timestampUs;
        });
        std::stable_sort(hairpins.begin(), hairpins.end(), [](const auto& left, const auto& right) {
            if (left.startUs != right.startUs) return left.startUs < right.startUs;
            return left.endUs < right.endUs;
        });
    }
}

// 先查找完整 trackId，再对层级 voice/staff ID 回退到父 track 的动态。
int PlaybackContext::velocityAt(const QString& trackId, qint64 timestampUs, int fallback) const
{
    auto it = m_dynamics.find(trackId);
    if (it == m_dynamics.end()) {
        const int slash = trackId.indexOf(u'/');
        if (slash > 0) it = m_dynamics.find(trackId.left(slash));
    }
    int result = fallback;
    if (it != m_dynamics.end()) {
        // 动态点采用“最后一个不晚于当前位置”的阶梯插值。
        for (const auto& point : it.value()) {
            if (point.timestampUs > timestampUs) break;
            result = point.velocity;
        }
    }
    auto hairpinIt = m_hairpins.find(trackId);
    if (hairpinIt == m_hairpins.end()) {
        const int slash = trackId.indexOf(u'/');
        if (slash > 0) hairpinIt = m_hairpins.find(trackId.left(slash));
    }
    if (hairpinIt != m_hairpins.end()) {
        // 多个 hairpin 可叠加；每段使用局部进度计算线性力度修正。
        for (const auto& hairpin : hairpinIt.value()) {
            if (timestampUs < hairpin.startUs || timestampUs > hairpin.endUs
                || (timestampUs == hairpin.endUs && !hairpin.includeEnd)) {
                continue;
            }
            const double localProgress = static_cast<double>(timestampUs - hairpin.startUs)
                                       / static_cast<double>(hairpin.endUs - hairpin.startUs);
            const double ratio = hairpin.startProgress
                               + (hairpin.endProgress - hairpin.startProgress) * localProgress;
            const int startVelocity = hairpin.crescendo ? result : std::max(1, result - 36);
            const int endVelocity = hairpin.crescendo ? std::min(127, result + 36) : result;
            result = static_cast<int>(std::lround(startVelocity + (endVelocity - startVelocity) * ratio));
        }
    }
    return result;
}

// QMap::upperBound 返回严格大于目标时间的项，前一个项即为当前 tempo。
double PlaybackContext::tempoAt(qint64 timestampUs) const
{
    auto it = m_tempos.upperBound(timestampUs);
    if (it == m_tempos.begin()) return 120.0;
    --it;
    return it.value();
}

} // namespace midi_play::playback
