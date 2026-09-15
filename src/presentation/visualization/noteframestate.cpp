#include "noteframestate.h"

#include <algorithm>
#include <cmath>

namespace midi_play::presentation::visualization {

QColor illuminatedKeyColor(const QColor& base, const QColor& light, qreal strength)
{
    const qreal alpha = std::clamp(strength, 0.0, 1.0);
    return QColor::fromRgbF(base.redF() * (1 - alpha) + light.redF() * alpha,
                           base.greenF() * (1 - alpha) + light.greenF() * alpha,
                           base.blueF() * (1 - alpha) + light.blueF() * alpha);
}

NoteGeometry noteGeometry(const PreparedNoteRenderData& note,
                          const PlaybackSceneGeometry& geometry, qint64 positionUs)
{
    const auto y = [&](qint64 time) {
        return geometry.strikeLineY - (time - positionUs) * geometry.pixelsPerMicrosecond;
    };
    const qreal bottom = y(note.startUs);
    const qreal top = std::min(y(note.keyEndUs), bottom - kNoteMinimumHeight);
    NoteGeometry result;
    result.body = {note.left, top, note.width, bottom - top};
    const qreal headHeight = std::min(kNoteHeadHeight, result.body.height());
    result.head = {note.left, bottom - headHeight, note.width, headHeight};
    if (note.hasTail) {
        result.tail = {note.left + note.width * (1 - kNoteTailWidth) * 0.5,
                       y(note.audibleEndUs), note.width * kNoteTailWidth,
                       std::max<qreal>(0, y(note.keyEndUs) - y(note.audibleEndUs))};
    }
    result.opacity = std::clamp(1.0 - qreal(positionUs - note.audibleEndUs)
        / kNoteReleaseUs, 0.0, 1.0);
    return result;
}

void NoteFrameState::prepare(const midi_play::visualization::PlaybackSceneState& state,
                             const PlaybackSceneGeometry& geometry, const NoteRenderCache& cache)
{
    m_keys.fill({});
    m_drums.fill({}, state.chart ? state.chart->drumLanes().size() : 0);
    m_glows.clear();
    m_active.reset(m_drums.size());
    m_visibleCount = m_activeCount = 0;
    m_bodyOpacity = 1;
    if (!state.chart) return;
    const bool playing = state.transportState == midi_play::playback::State::Playing;
    const qint64 now = state.transportPositionUs;
    qreal area = 0;
    for (const int index : state.candidateNoteIndices) {
        const auto* prepared = cache.note(index);
        if (!prepared || !prepared->validGeometry) continue;
        const auto& note = state.chart->notes()[index];
        if (note.startUs > state.visibleWindowEndUs || note.audibleEndUs < state.visibleWindowStartUs) continue;
        ++m_visibleCount;
        const auto shape = noteGeometry(*prepared, geometry, now);
        const QRectF visible = shape.body.intersected(geometry.fallingRect);
        area += visible.width() * visible.height();
        if (!playing) continue;
        if (note.startUs <= now && note.audibleEndUs > now) {
            m_active.add(index, note);
            ++m_activeCount;
        }
        if (note.startUs > now || now > note.audibleEndUs + kNoteReleaseUs) continue;
        const auto* style = cache.styleForNote(index);
        if (!style) continue;
        const qreal age = qreal(now - note.startUs);
        const qreal attack = note.startUs >= state.effectsStartUs
            ? std::pow(std::clamp(1 - age / kNoteAttackUs, 0.0, 1.0), 2) : 0;
        const qreal hold = now < note.keyEndUs ? 0.56 : now < note.audibleEndUs ? 0.15
            : 0.15 * std::clamp(1 - qreal(now - note.audibleEndUs) / kNoteReleaseUs, 0.0, 1.0);
        const qreal strength = std::min<qreal>(0.95, (hold + attack * 0.35) * style->material.energy);
        KeyIllumination* key = nullptr;
        if (note.isPercussion()) {
            if (note.drumLane >= 0 && note.drumLane < m_drums.size()) key = &m_drums[note.drumLane];
        } else if (note.pitch >= 0 && note.pitch < 128) key = &m_keys[size_t(note.pitch)];
        if (key && strength >= key->strength) *key = {index, strength, attack};
    }
    const qreal sceneArea = std::max<qreal>(1, geometry.pianoRect.width() * geometry.fallingRect.height());
    m_bodyOpacity = std::clamp(1.0 / (1 + area / sceneArea * 0.42), 0.52, 1.0);
    if (!playing) return;
    const auto appendGlow = [&](const KeyIllumination& key, qreal x, qreal width) {
        if (key.noteIndex < 0 || m_glows.size() >= 96) return;
        const auto* style = cache.styleForNote(key.noteIndex);
        QColor color = style->material.head;
        color.setAlphaF(std::min<qreal>(0.38, key.strength * 0.24 + key.attack * 0.16));
        const qreal radius = std::clamp(width * 0.9 + key.attack * 8, 7.0, 28.0);
        m_glows.push_back({{x - radius, geometry.strikeLineY - radius * 0.6,
                           radius * 2, radius * 1.2}, color});
        // Two deterministic, short-lived sparks only on recent attacks.
        if (key.attack > 0.04 && m_glows.size() < 94) {
            const qreal travel = (1 - std::sqrt(key.attack)) * 17;
            color.setAlphaF(key.attack * 0.60 * m_bodyOpacity);
            for (int direction : {-1, 1}) {
                m_glows.push_back({{x + direction * travel * 0.45 - 1.4,
                    geometry.strikeLineY - 4 - travel, 2.8, 4.0}, color});
            }
        }
    };
    for (int pitch = 0; pitch < 128; ++pitch) {
        if (const auto* slot = geometry.pitchSlot(pitch)) appendGlow(m_keys[size_t(pitch)], slot->centerX, slot->noteWidth);
    }
    for (int lane = 0; lane < m_drums.size(); ++lane) {
        if (const auto* slot = geometry.drumSlot(lane)) appendGlow(m_drums[lane], slot->centerX, slot->noteWidth);
    }
}

} // namespace midi_play::presentation::visualization
