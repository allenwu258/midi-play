#pragma once

#include "app/introtextconfig.h"
#include <QImage>
#include <QRectF>

class QPainter;

namespace midi_play::presentation::visualization {

struct IntroTextAnimationState {
    qreal opacity = 0.0;
    qreal offsetY = 0.0; // Output pixels.
};

// One prepared image per layout/output size. Preview and export share these semantics.
class IntroTextAsset final {
public:
    static constexpr QSize referenceCanvas {1280, 720};
    bool prepare(const app::IntroTextConfig& config, QSize outputSize, QString* error);
    static QString validationError(const app::IntroTextConfig& config);
    static QRectF blockBounds(const app::IntroTextConfig& config, QSizeF blockSize);

    bool isValid() const noexcept { return m_valid; }
    const QRectF& logicalBounds() const noexcept { return m_bounds; }
    QRectF outputBounds() const;
    QSize outputSize() const noexcept { return m_outputSize; }
    IntroTextAnimationState animationState(qint64 outputTimeUs) const;
    void paint(QPainter& painter, qint64 outputTimeUs, bool forceVisible = false) const;
    // Returns before creating a painter when disabled or fully hidden.
    bool composite(QImage& frame, qint64 outputTimeUs) const;

private:
    app::IntroTextConfig m_config;
    QSize m_outputSize;
    QRectF m_bounds;
    QImage m_image;
    bool m_valid = false;
};

} // namespace midi_play::presentation::visualization
