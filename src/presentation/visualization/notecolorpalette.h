#pragma once

#include "domain/visualization/visualchart.h"

#include <QColor>

namespace midi_play::presentation::visualization {

// Angles use radians. Conversion returns straight-alpha sRGB, reducing chroma
// into the display gamut without clipping individual linear RGB channels.
double noteColorHue(const midi_play::visualization::ColorRgba& color);
QColor noteColorFromOklch(double lightness, double chroma, double angle, double alpha);
double vividNoteHue(int trackIndex, int pitch, bool percussion);

} // namespace midi_play::presentation::visualization
