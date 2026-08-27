#include "playbackcontext.h"

#include <algorithm>

namespace midi_play::playback {

PlaybackContext::PlaybackContext(std::shared_ptr<const music::MusicDocument> document)
{
    if (!document) return;
    for (const auto& tempo : document->tempos()) {
        m_tempos.insert(document->tickToMicroseconds(tempo.tick), tempo.bpm);
    }
    for (const auto& track : document->tracks()) {
        auto& points = m_dynamics[track.id];
        for (const auto& note : track.notes) {
            if (note.velocity != 90) {
                points.push_back({document->tickToMicroseconds(note.start), note.velocity});
            }
        }
    }
}

int PlaybackContext::velocityAt(const QString& trackId, qint64 timestampUs, int fallback) const
{
    auto it = m_dynamics.find(trackId);
    if (it == m_dynamics.end()) {
        const int slash = trackId.indexOf(u'/');
        if (slash > 0) it = m_dynamics.find(trackId.left(slash));
    }
    if (it == m_dynamics.end()) return fallback;
    int result = fallback;
    for (const auto& point : it.value()) {
        if (point.timestampUs > timestampUs) break;
        result = point.velocity;
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
