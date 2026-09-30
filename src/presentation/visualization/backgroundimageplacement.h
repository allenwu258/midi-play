#pragma once

#include "domain/settings/backgroundimagealignment.h"

#include <QRectF>
#include <QSizeF>

namespace midi_play::presentation::visualization {

struct BackgroundImagePlacement {
    QRectF destination;
    // Normalized source rectangle. Values are always clamped to [0, 1].
    QRectF sourceUv;
};

BackgroundImagePlacement calculateBackgroundImagePlacement(
    QSizeF sourceSize, QRectF targetRect,
    midi_play::settings::BackgroundImageAlignment alignment);

} // namespace midi_play::presentation::visualization
