#include "playbackeventsrenderer.h"

// 实现力度/演奏法修正、表达曲线物化和 tie 音符合并。
#include <algorithm>

namespace midi_play::playback {

// 动态上下文由 PlaybackModel 共享，渲染器只保存只读引用。
PlaybackEventsRenderer::PlaybackEventsRenderer(std::shared_ptr<const PlaybackContext> context)
    : m_context(std::move(context))
{
}

// 应用动态和演奏法，再把变化的表达曲线展开为传统 MIDI 可消费的事件。
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
        // 演奏法按照输入标记叠加：时值修正作用于 duration，力度修正作用于 velocity。
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

        // 为传统 MIDI 后端物化表达曲线。支持 per-note/MPE 的后端可以直接消费
        // 原始上下文；CC11 和通道 pitch bend 是 FluidSynth/GM 设备的通用回退。
        auto hasVariation = [](const QVector<ExpressionCurvePoint>& curve) {
            if (curve.size() < 2) return false;
            const float first = curve.front().value;
            for (const auto& point : curve) {
                if (qAbs(point.value - first) > 0.001F) return true;
            }
            return false;
        };
        if (hasVariation(event.expression.expressionCurve)) {
            // 音量曲线映射为 0..127 的表达控制值。
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
            // 归一化音高曲线映射为 14 位 MIDI pitch bend 值。
            for (const auto& point : event.expression.pitchCurve) {
                PlaybackEvent pitch;
                pitch.timestampUs = event.timestampUs + qMax<qint64>(0, point.offsetUs);
                pitch.channel = event.channel;
                // 音高曲线使用与半音无关的 [-1, 1] 归一化值，映射到 14 位 MIDI
                // pitch bend 范围。
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
    // 新增表达事件与原事件统一按协议优先级排序。
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
            // 相邻 tie 音符合并为一个持续事件，避免后端在连接处重新触发。
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
