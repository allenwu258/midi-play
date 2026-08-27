#pragma once

#include <QRectF>
#include <QSizeF>
#include <QVector>

namespace midi_play::presentation::visualization {

struct PitchSlotGeometry {
    int pitch = -1;
    qreal centerX = 0.0;
    qreal noteWidth = 0.0;
    QRectF keyRect;
    bool blackKey = false;
    bool valid = false;
};

struct DrumSlotGeometry {
    int lane = -1;
    QRectF keyRect;
    qreal centerX = 0.0;
    qreal noteWidth = 0.0;
};

struct PlaybackSceneGeometry {
    QSizeF viewportSize;
    QRectF bounds;
    QRectF informationRect;
    QRectF fallingRect;
    QRectF keyboardRect;
    QRectF pianoRect;
    QRectF drumRect;
    qreal strikeLineY = 0.0;
    qreal pixelsPerMicrosecond = 0.0;
    QVector<PitchSlotGeometry> pitches;
    QVector<DrumSlotGeometry> drumSlots;

    const PitchSlotGeometry* pitchSlot(int pitch) const
    {
        return pitch >= 0 && pitch < pitches.size() && pitches[pitch].valid ? &pitches[pitch] : nullptr;
    }

    const DrumSlotGeometry* drumSlot(int lane) const
    {
        return lane >= 0 && lane < drumSlots.size() ? &drumSlots[lane] : nullptr;
    }
};

} // namespace midi_play::presentation::visualization
