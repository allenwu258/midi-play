#include "scenelayoutengine.h"

#include <algorithm>

namespace midi_play::presentation::visualization {
namespace {

bool isWhitePitchClass(int pitchClass)
{
    switch ((pitchClass + 12) % 12) {
    case 0:
    case 2:
    case 4:
    case 5:
    case 7:
    case 9:
    case 11:
        return true;
    default:
        return false;
    }
}

} // namespace

bool SceneLayoutEngine::isBlackKey(int pitch)
{
    return !isWhitePitchClass(pitch % 12);
}

PlaybackSceneGeometry SceneLayoutEngine::layout(const QSizeF& viewport,
                                                const midi_play::visualization::VisualChart* chart,
                                                qint64 lookAheadUs) const
{
    PlaybackSceneGeometry geometry;
    geometry.bounds = QRectF(QPointF(0.0, 0.0), viewport);

    const qreal width = std::max<qreal>(1.0, viewport.width());
    const qreal height = std::max<qreal>(1.0, viewport.height());
    const qreal keyboardHeight = std::clamp(height * 0.17, 88.0, 132.0);
    const qreal bottom = height;
    geometry.keyboardRect = QRectF(0.0, bottom - keyboardHeight, width, keyboardHeight);
    geometry.fallingRect = QRectF(0.0, 0.0, width,
                                  std::max<qreal>(1.0, geometry.keyboardRect.top()));
    geometry.strikeLineY = geometry.fallingRect.bottom() - std::clamp(height * 0.045, 24.0, 38.0);
    geometry.pixelsPerMicrosecond = std::max<qreal>(0.000001,
        (geometry.strikeLineY - geometry.fallingRect.top()) / std::max<qint64>(1, lookAheadUs));

    const qreal measureGutter = width < 720.0 ? 36.0 : 50.0;
    const int drumCount = chart ? chart->drumLanes().size() : 0;
    const bool hasMelodicNotes = chart && std::any_of(chart->tracks().cbegin(), chart->tracks().cend(),
                                                     [](const auto& track) {
                                                         return !track.percussion && !track.noteIndices.isEmpty();
                                                     });
    qreal drumWidth = 0.0;
    qreal gap = 0.0;
    if (drumCount > 0) {
        if (hasMelodicNotes) {
            drumWidth = std::clamp(width * 0.19, 112.0, 250.0);
            if (width < 700.0) drumWidth = std::min<qreal>(drumWidth, width * 0.25);
            gap = 8.0;
        } else {
            drumWidth = std::max<qreal>(1.0, width - measureGutter);
        }
    }
    const qreal pitchWidth = hasMelodicNotes || drumCount == 0
        ? std::max<qreal>(80.0, width - measureGutter - drumWidth - gap) : 0.0;
    geometry.pianoRect = QRectF(measureGutter, geometry.keyboardRect.top(), pitchWidth, keyboardHeight);
    if (drumCount > 0) {
        geometry.drumRect = QRectF(geometry.pianoRect.right() + gap, geometry.keyboardRect.top(),
                                   std::max<qreal>(1.0, width - geometry.pianoRect.right() - gap), keyboardHeight);
    }

    const auto range = chart ? chart->pitchRange() : midi_play::visualization::PitchRange {};
    const int minimumPitch = std::clamp(range.minimum, 0, 127);
    const int maximumPitch = std::clamp(range.maximum, minimumPitch, 127);
    int whiteCount = 0;
    for (int pitch = minimumPitch; pitch <= maximumPitch; ++pitch) {
        if (!isBlackKey(pitch)) ++whiteCount;
    }
    whiteCount = std::max(1, whiteCount);
    const qreal whiteWidth = geometry.pianoRect.width() / whiteCount;
    geometry.pitches.resize(128);
    int whiteOrdinal = 0;
    for (int pitch = minimumPitch; pitch <= maximumPitch; ++pitch) {
        auto& slot = geometry.pitches[pitch];
        slot.pitch = pitch;
        slot.blackKey = isBlackKey(pitch);
        slot.valid = true;
        if (!slot.blackKey) {
            const qreal left = geometry.pianoRect.left() + whiteOrdinal * whiteWidth;
            slot.keyRect = QRectF(left, geometry.keyboardRect.top(), whiteWidth, keyboardHeight);
            slot.centerX = left + whiteWidth * 0.5;
            slot.noteWidth = std::max<qreal>(2.0, whiteWidth * 0.76);
            ++whiteOrdinal;
        } else {
            const qreal center = geometry.pianoRect.left() + whiteOrdinal * whiteWidth;
            const qreal blackWidth = std::max<qreal>(2.0, whiteWidth * 0.62);
            slot.keyRect = QRectF(center - blackWidth * 0.5, geometry.keyboardRect.top(),
                                  blackWidth, keyboardHeight * 0.62);
            slot.centerX = center;
            slot.noteWidth = std::max<qreal>(2.0, blackWidth * 0.82);
        }
    }

    if (drumCount > 0) {
        const qreal laneWidth = geometry.drumRect.width() / drumCount;
        geometry.drumSlots.reserve(drumCount);
        for (int lane = 0; lane < drumCount; ++lane) {
            const QRectF keyRect(geometry.drumRect.left() + lane * laneWidth,
                                 geometry.keyboardRect.top(), laneWidth, keyboardHeight);
            geometry.drumSlots.push_back({lane, keyRect, keyRect.center().x(),
                                          std::max<qreal>(2.0, laneWidth * 0.76)});
        }
    }
    return geometry;
}

} // namespace midi_play::presentation::visualization
