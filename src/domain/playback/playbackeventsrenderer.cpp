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
    QVector<PlaybackEvent> expressionEvents;
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

        // Materialize curves for conventional MIDI backends. A backend with
        // true per-note/MPE support can later consume the original expression
        // context instead; CC11 and channel pitch bend are the interoperable
        // fallback for FluidSynth and General MIDI devices.
        auto hasVariation = [](const QVector<ExpressionCurvePoint>& curve) {
            if (curve.size() < 2) return false;
            const float first = curve.front().value;
            for (const auto& point : curve) {
                if (qAbs(point.value - first) > 0.001F) return true;
            }
            return false;
        };
        if (hasVariation(event.expression.expressionCurve)) {
            for (const auto& point : event.expression.expressionCurve) {
                PlaybackEvent expression;
                expression.timestampUs = event.timestampUs + qMax<qint64>(0, point.offsetUs);
                expression.channel = event.channel;
                expression.value = qBound(0, qRound(point.value * 127.0F), 127);
                expression.noteId = event.noteId >= 0 ? event.noteId
                                                       : event.channel * 128 + event.pitch;
                expression.expressionType = NoteExpressionType::Volume;
                expression.voice = event.voice;
                expression.staff = event.staff;
                expression.kind = PlaybackEventKind::NoteExpression;
                expression.sequence = sequence++;
                expressionEvents.push_back(std::move(expression));
            }
        }
        if (hasVariation(event.expression.pitchCurve)) {
            for (const auto& point : event.expression.pitchCurve) {
                PlaybackEvent pitch;
                pitch.timestampUs = event.timestampUs + qMax<qint64>(0, point.offsetUs);
                pitch.channel = event.channel;
                // Pitch curves use normalized semitone-independent [-1, 1]
                // values and are mapped to the 14-bit MIDI bend range.
                pitch.value = qBound(0, qRound(8192.0F + point.value * 8191.0F), 16383);
                pitch.noteId = event.noteId >= 0 ? event.noteId
                                                 : event.channel * 128 + event.pitch;
                pitch.expressionType = NoteExpressionType::Pitch;
                pitch.voice = event.voice;
                pitch.staff = event.staff;
                pitch.kind = PlaybackEventKind::NoteExpression;
                pitch.sequence = sequence++;
                expressionEvents.push_back(std::move(pitch));
            }
        }
    }
    result += expressionEvents;
    std::sort(result.begin(), result.end(), [](const auto& left, const auto& right) {
        if (left.timestampUs != right.timestampUs) return left.timestampUs < right.timestampUs;
        const int leftPriority = playbackEventPriority(left);
        const int rightPriority = playbackEventPriority(right);
        if (leftPriority != rightPriority) return leftPriority < rightPriority;
        return left.sequence < right.sequence;
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
