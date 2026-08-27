#include "playbackcontext.h"

#include <algorithm>
#include <cmath>

namespace midi_play::playback {

PlaybackContext::PlaybackContext(std::shared_ptr<const music::MusicDocument> document)
{
    if (!document) return;
    const auto segments = document->playbackSegments();
    for (const auto& segment : segments) {
        for (const auto& tempo : document->tempos()) {
            if (tempo.tick < segment.sourceStart || tempo.tick >= segment.sourceEnd) continue;
            const music::Tick outputTick = segment.outputStart + (tempo.tick - segment.sourceStart);
            m_tempos.insert(document->playbackTickToMicroseconds(outputTick), tempo.bpm);
        }
    }
    for (const auto& track : document->tracks()) {
        auto& points = m_dynamics[track.id];
        for (const auto& dynamic : track.dynamics) {
            points.push_back({document->playbackTickToMicroseconds(dynamic.tick), dynamic.velocity});
        }
        for (const auto& note : track.notes) {
            if (note.velocity != 90) {
                points.push_back({document->playbackTickToMicroseconds(note.start), note.velocity});
            }
        }
        auto& hairpins = m_hairpins[track.id];
        for (const auto& hairpin : track.hairpins) {
            const qint64 startUs = document->playbackTickToMicroseconds(hairpin.start);
            const qint64 endUs = document->playbackTickToMicroseconds(hairpin.end);
            if (endUs > startUs) hairpins.push_back({startUs, endUs, hairpin.crescendo});
        }
        std::sort(points.begin(), points.end(), [](const auto& left, const auto& right) {
            return left.timestampUs < right.timestampUs;
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
            if (timestampUs < hairpin.startUs || timestampUs > hairpin.endUs) continue;
            const double ratio = static_cast<double>(timestampUs - hairpin.startUs)
                               / static_cast<double>(hairpin.endUs - hairpin.startUs);
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
