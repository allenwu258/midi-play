#include "playbackeventsrenderer.h"

#include <algorithm>

namespace midi_play::playback {

PlaybackEventsRenderer::PlaybackEventsRenderer(std::shared_ptr<const PlaybackContext> context)
    : m_context(std::move(context))
{
}

QVector<PlaybackEvent> PlaybackEventsRenderer::render(const PlaybackData& source) const
{
    QVector<PlaybackEvent> result = source.events;
    for (auto& event : result) {
        event.velocity = m_context->velocityAt(source.trackId, event.timestampUs, event.velocity);
    }
    std::sort(result.begin(), result.end(), [](const auto& left, const auto& right) {
        return left.timestampUs < right.timestampUs;
    });
    return result;
}

} // namespace midi_play::playback
