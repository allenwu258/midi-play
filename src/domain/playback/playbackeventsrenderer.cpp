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
        if (event.staccato) {
            event.durationUs = qMax<qint64>(1'000, event.durationUs * 0.5);
        } else if (event.tenuto) {
            event.durationUs = event.durationUs * 0.98;
        }
        if (event.accent) event.velocity = qMin(127, event.velocity + 18);
        if (event.ghost) event.velocity = qMax(1, event.velocity / 2);
    }
    std::sort(result.begin(), result.end(), [](const auto& left, const auto& right) {
        return left.timestampUs < right.timestampUs;
    });
    QVector<PlaybackEvent> merged;
    merged.reserve(result.size());
    for (const auto& event : result) {
        if (!merged.isEmpty()) {
            auto& previous = merged.back();
            if (previous.kind == PlaybackEvent::Kind::Note && event.kind == PlaybackEvent::Kind::Note
                && previous.channel == event.channel && previous.pitch == event.pitch
                && previous.tieStart && event.tieStop
                && event.timestampUs <= previous.timestampUs + previous.durationUs + 2'000) {
                previous.durationUs = qMax(previous.durationUs,
                                           event.timestampUs + event.durationUs - previous.timestampUs);
                previous.tieStart = event.tieStart;
                continue;
            }
        }
        merged.push_back(event);
    }
    return merged;
}

} // namespace midi_play::playback
