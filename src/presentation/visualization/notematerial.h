#pragma once

#include "domain/visualization/visualchart.h"
#include "noteappearance.h"

#include <QColor>
#include <QHashFunctions>

namespace midi_play::presentation::visualization {

// A renderer-independent material. Colors are straight-alpha sRGB; each
// backend performs premultiplication exactly once at its output boundary.
struct NoteMaterial {
    QColor body;
    QColor head;
    QColor tail;
    QColor keyFill;
    QColor keyTop;
    QColor glow;
    float energy = 0.0f;
};

// Both color modes use this full identity, so recoloring never changes the
// mapping from notes to styles. Every input affecting material must be covered.
struct NoteStyleKey {
    int trackIndex;
    int pitch;
    int voiceTint;
    int velocityBucket;
    bool ghost;
    bool percussion;
    bool operator==(const NoteStyleKey&) const = default;
};

inline size_t qHash(const NoteStyleKey& key, size_t seed = 0) noexcept
{
    return qHashMulti(seed, key.trackIndex, key.pitch, key.voiceTint,
                      key.velocityBucket, key.ghost, key.percussion);
}

NoteStyleKey noteMaterialKey(const midi_play::visualization::VisualNote& note);
NoteMaterial makeNoteMaterial(const midi_play::visualization::VisualChart& chart,
                              const midi_play::visualization::VisualNote& note,
                              const NoteAppearance& appearance = noteAppearanceFor());

// Shared geometry/material constants; matching GLSL formulas live in note.*.
inline constexpr qreal kNoteMinimumHeight = 4.0;
inline constexpr qreal kNoteHeadHeight = 2.5;
inline constexpr qreal kNoteTailWidth = 0.38;
inline constexpr qint64 kNoteReleaseUs = 240'000;
inline constexpr qint64 kNoteAttackUs = 180'000;

} // namespace midi_play::presentation::visualization
