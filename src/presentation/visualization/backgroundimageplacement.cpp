#include "backgroundimageplacement.h"

#include <algorithm>

namespace midi_play::presentation::visualization {
namespace {

QRectF normalizedRect(qreal left, qreal top, qreal width, qreal height)
{
    return QRectF(std::clamp(left, 0.0, 1.0), std::clamp(top, 0.0, 1.0),
                  std::clamp(width, 0.0, 1.0), std::clamp(height, 0.0, 1.0));
}

} // namespace

BackgroundImagePlacement calculateBackgroundImagePlacement(
    QSizeF sourceSize, QRectF targetRect,
    midi_play::settings::BackgroundImageAlignment requestedAlignment)
{
    BackgroundImagePlacement result;
    if (sourceSize.width() <= 0.0 || sourceSize.height() <= 0.0 || targetRect.isEmpty())
        return result;

    const auto alignment = midi_play::settings::normalizeBackgroundImageAlignment(requestedAlignment);
    const qreal sourceAspect = sourceSize.width() / sourceSize.height();
    const qreal targetAspect = targetRect.width() / targetRect.height();
    const bool contain = alignment == midi_play::settings::BackgroundImageAlignment::Contain;

    if (contain) {
        const qreal scale = std::min(targetRect.width() / sourceSize.width(),
                                     targetRect.height() / sourceSize.height());
        const QSizeF fitted = sourceSize * scale;
        result.destination = QRectF(targetRect.center() - QPointF(fitted.width(), fitted.height()) / 2.0,
                                    fitted);
        result.sourceUv = QRectF(0.0, 0.0, 1.0, 1.0);
        return result;
    }

    // Cover modes fill the whole target. The UV rectangle expresses the part
    // of the fitted image that remains visible after cropping.
    if (sourceAspect > targetAspect) {
        const qreal visibleWidth = targetAspect / sourceAspect;
        qreal left = (1.0 - visibleWidth) * 0.5;
        switch (alignment) {
        case midi_play::settings::BackgroundImageAlignment::Left: left = 0.0; break;
        case midi_play::settings::BackgroundImageAlignment::Right: left = 1.0 - visibleWidth; break;
        default: break;
        }
        result.sourceUv = normalizedRect(left, 0.0, visibleWidth, 1.0);
    } else {
        const qreal visibleHeight = sourceAspect / targetAspect;
        qreal top = (1.0 - visibleHeight) * 0.5;
        switch (alignment) {
        case midi_play::settings::BackgroundImageAlignment::Top: top = 0.0; break;
        case midi_play::settings::BackgroundImageAlignment::Bottom: top = 1.0 - visibleHeight; break;
        default: break;
        }
        result.sourceUv = normalizedRect(0.0, top, 1.0, visibleHeight);
    }
    result.destination = targetRect;
    return result;
}

} // namespace midi_play::presentation::visualization
