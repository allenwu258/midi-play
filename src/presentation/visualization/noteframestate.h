#pragma once

#include "domain/visualization/activenotelookup.h"
#include "domain/visualization/playbackscenestate.h"
#include "noterendercache.h"

#include <QRectF>
#include <QVector>
#include <array>

namespace midi_play::presentation::visualization {

struct NoteGeometry {
    QRectF body;
    QRectF tail;
    QRectF head;
    qreal opacity = 1;
};

NoteGeometry noteGeometry(const PreparedNoteRenderData& note,
                          const PlaybackSceneGeometry& geometry, qint64 positionUs);
QColor illuminatedKeyColor(const QColor& base, const QColor& light, qreal strength);

struct KeyIllumination {
    int noteIndex = -1;
    qreal strength = 0;
    qreal attack = 0;
};

struct NoteGlow {
    QRectF rect;
    QColor color;
};

// Transient state is evaluated from song time, not integrated per rendered
// frame. Dropped frames, seeks and backend switches cannot duplicate effects.
class NoteFrameState final {
public:
    void prepare(const midi_play::visualization::PlaybackSceneState& state,
                 const PlaybackSceneGeometry& geometry, const NoteRenderCache& cache);
    const KeyIllumination& key(int pitch) const { return m_keys.at(size_t(pitch)); }
    const KeyIllumination& drum(int lane) const { return m_drums.at(lane); }
    const QVector<NoteGlow>& glows() const { return m_glows; }
    const midi_play::visualization::ActiveNoteLookup& active() const { return m_active; }
    qreal bodyOpacity() const { return m_bodyOpacity; }
    qsizetype visibleCount() const { return m_visibleCount; }
    qsizetype activeCount() const { return m_activeCount; }

private:
    std::array<KeyIllumination, 128> m_keys;
    QVector<KeyIllumination> m_drums;
    QVector<NoteGlow> m_glows;
    midi_play::visualization::ActiveNoteLookup m_active;
    qreal m_bodyOpacity = 1;
    qsizetype m_visibleCount = 0;
    qsizetype m_activeCount = 0;
};

} // namespace midi_play::presentation::visualization
