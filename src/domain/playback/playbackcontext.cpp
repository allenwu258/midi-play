#include "playbackcontext.h"

#include <algorithm>
#include <cmath>

namespace midi_play::playback {

PlaybackContext::PlaybackContext(std::shared_ptr<const music::MusicDocument> document,
                                 std::shared_ptr<const music::PlaybackTimeline> timeline)
{
    if (!document || !timeline) return;
    const auto& segments = timeline->segments();
    for (const auto& segment : segments) {
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
        std::stable_sort(sortedDynamics.begin(), sortedDynamics.end(), [](const auto* left, const auto* right) {
            return left->tick < right->tick;
        });

        QVector<const music::NoteEvent*> sortedVelocityNotes;
        sortedVelocityNotes.reserve(track.notes.size());
        for (const auto& note : track.notes) {
            if (note.velocity != 90) sortedVelocityNotes.push_back(&note);
        }
        std::stable_sort(sortedVelocityNotes.begin(), sortedVelocityNotes.end(), [](const auto* left, const auto* right) {
            return left->start < right->start;
        });

        for (const auto& segment : segments) {
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

int PlaybackContext::velocityAt(const QString& trackId, qint64 timestampUs, int fallback) const
{
    auto it = m_dynamics.find(trackId);
    if (it == m_dynamics.end()) {
        const int slash = trackId.indexOf(u'/');
        if (slash > 0) it = m_dynamics.find(trackId.left(slash));
    }
    int result = fallback;
    if (it != m_dynamics.end()) {
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

double PlaybackContext::tempoAt(qint64 timestampUs) const
{
    auto it = m_tempos.upperBound(timestampUs);
    if (it == m_tempos.begin()) return 120.0;
    --it;
    return it.value();
}

} // namespace midi_play::playback
