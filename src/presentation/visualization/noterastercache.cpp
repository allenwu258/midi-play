#include "noterastercache.h"

#include <QPainter>

#include <algorithm>
#include <memory>

namespace midi_play::presentation::visualization {

void NoteRasterCache::prepare(const NoteRenderCache& notes, qreal devicePixelRatio)
{
    const qreal dpr = std::max<qreal>(1, devicePixelRatio);
    if (m_chartRevision == notes.chartBuildCount()
        && m_geometryRevision == notes.geometryBuildCount()
        && m_materialRevision == notes.materialRevision() && m_dpr == dpr) return;
    m_chartRevision = notes.chartBuildCount();
    m_geometryRevision = notes.geometryBuildCount();
    m_materialRevision = notes.materialRevision();
    m_dpr = dpr;
    m_images.clear();
}

void NoteRasterCache::drawBody(QPainter& painter, const QRectF& rect,
                               const NoteRenderStyle& style, int styleIndex)
{
    const quint64 key = (quint64(styleIndex) << 32) | quint32(qRound(rect.width() * 256));
    QImage* image = m_images.object(key);
    std::unique_ptr<QImage> created;
    if (!image) {
        created = std::make_unique<QImage>(qCeil(rect.width() * m_dpr) + 2, 5,
                                          QImage::Format_ARGB32_Premultiplied);
        created->fill(Qt::transparent);
        QPainter raster(created.get());
        raster.scale(m_dpr, m_dpr);
        raster.setRenderHint(QPainter::Antialiasing);
        raster.fillRect(QRectF(1 / m_dpr, 1 / m_dpr, rect.width(), 3 / m_dpr), style.bodyGradientBrush);
        raster.end();
        const int cost = int(created->sizeInBytes() / 1024) + 1;
        image = created.get();
        if (cost <= m_images.maxCost()) m_images.insert(key, created.release(), cost);
    }

    const qreal padding = 1 / m_dpr;
    const qreal left = rect.left() - padding;
    const qreal width = image->width() / m_dpr;
    // One transparent border row and one filled row at each end. The middle
    // row has filled neighbours, so bilinear sampling cannot fade a long body.
    painter.drawImage(QRectF(left, rect.top() - padding, width, 2 * padding), *image,
                      QRectF(0, 0, image->width(), 2));
    painter.drawImage(QRectF(left, rect.top() + padding, width, rect.height() - 2 * padding), *image,
                      QRectF(0, 2, image->width(), 1));
    painter.drawImage(QRectF(left, rect.bottom() - padding, width, 2 * padding), *image,
                      QRectF(0, 3, image->width(), 2));
}

} // namespace midi_play::presentation::visualization
