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
    quint64 sequence = 0;
    for (auto& event : result) {
        event.sequence = sequence++;
        if (event.kind != PlaybackEventKind::NoteOn) {
            continue;
        }
        event.velocity = m_context->velocityAt(source.trackId, event.timestampUs, event.velocity);
        double durationFactor = 1.0;
        int velocityOffset = 0;
        if (event.staccato) {
            event.expression.articulations.push_back({ArticulationType::Staccato, 0.5, 1.0, 0});
            durationFactor *= 0.5;
        }
        if (event.tenuto) {
            event.expression.articulations.push_back({ArticulationType::Tenuto, 0.98, 1.0, 0});
            durationFactor *= 0.98;
        }
        if (event.accent) {
            event.expression.articulations.push_back({ArticulationType::Accent, 1.0, 1.0, 18});
            velocityOffset += 18;
        }
        if (event.marcato) {
            event.expression.articulations.push_back({ArticulationType::Marcato, 1.0, 1.0, 24});
            velocityOffset += 24;
        }
        if (event.ghost) {
            event.expression.articulations.push_back({ArticulationType::Ghost, 1.0, 0.5, 0});
            event.velocity = qMax(1, event.velocity / 2);
        }
        event.durationUs = qMax<qint64>(1'000, static_cast<qint64>(event.durationUs * durationFactor));
        event.velocity = qBound(1, event.velocity + velocityOffset, 127);
        event.expression.expressionCurve = {
            {0, static_cast<float>(event.velocity) / 127.0F},
            {event.durationUs, static_cast<float>(event.velocity) / 127.0F}
        };
        if (event.tremolo) {
            event.expression.articulations.push_back({ArticulationType::Tremolo, 1.0, 1.0, 0});
            event.expression.expressionCurve.clear();
            constexpr int kTremoloSteps = 8;
            for (int step = 0; step <= kTremoloSteps; ++step) {
                const qint64 offset = event.durationUs * step / kTremoloSteps;
                const float level = (step % 2 == 0 ? 1.0F : 0.72F)
                                  * static_cast<float>(event.velocity) / 127.0F;
                event.expression.expressionCurve.push_back({offset, level});
            }
        }
    }
    std::sort(result.begin(), result.end(), [](const auto& left, const auto& right) {
        return left.timestampUs < right.timestampUs;
    });
    QVector<PlaybackEvent> merged;
    merged.reserve(result.size());
    for (const auto& event : result) {
        if (!merged.isEmpty()) {
            auto& previous = merged.back();
            if (previous.kind == PlaybackEventKind::NoteOn && event.kind == PlaybackEventKind::NoteOn
                && previous.channel == event.channel && previous.pitch == event.pitch
                && previous.voice == event.voice && previous.staff == event.staff
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
