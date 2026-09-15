#pragma once

#include "noterendercache.h"

#include <QCache>
#include <QImage>
#include <QRectF>

class QPainter;

namespace midi_play::presentation::visualization {

// Cache a few rows of each rectangular body gradient. Image size is independent
// of note duration; edge rows preserve subpixel motion and square corners.
class NoteRasterCache final {
public:
    void prepare(const NoteRenderCache& notes, qreal devicePixelRatio);
    void drawBody(QPainter& painter, const QRectF& rect,
                  const NoteRenderStyle& style, int styleIndex);

private:
    QCache<quint64, QImage> m_images {16 * 1024}; // KiB
    quint64 m_chartRevision = 0;
    quint64 m_geometryRevision = 0;
    qreal m_dpr = 0;
};

} // namespace midi_play::presentation::visualization
