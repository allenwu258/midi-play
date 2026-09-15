#pragma once

#include "domain/visualization/visualchart.h"

#include <QColor>

namespace midi_play::presentation::visualization {

// A renderer-independent material. Colors are straight-alpha sRGB; each
// backend performs premultiplication exactly once at its output boundary.
struct NoteMaterial {
    QColor body;
    QColor head;
    QColor tail;
    float energy = 0.0f;
};

quint64 noteMaterialKey(const midi_play::visualization::VisualNote& note);
NoteMaterial makeNoteMaterial(const midi_play::visualization::VisualChart& chart,
                              const midi_play::visualization::VisualNote& note);

// Shared geometry/material constants; matching GLSL formulas live in note.*.
inline constexpr qreal kNoteMinimumHeight = 4.0;
inline constexpr qreal kNoteHeadHeight = 2.5;
inline constexpr qreal kNoteTailWidth = 0.38;
inline constexpr qint64 kNoteReleaseUs = 240'000;
inline constexpr qint64 kNoteAttackUs = 180'000;

} // namespace midi_play::presentation::visualization
