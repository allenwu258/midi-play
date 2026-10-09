#pragma once

// Qt 栅格后端复用音符横向渐变小图，控制长音绘制开销。
#include "noterendercache.h"

#include <QCache>
#include <QImage>
#include <QRectF>

class QPainter;

namespace midi_play::presentation::visualization {

// 每个体渐变仅缓存少量像素行，图像大小与音符时长无关；边缘行保留亚像素运动。
class NoteRasterCache final {
public:
    // 图表、几何、材质或 DPI 变化时清空图像，其他帧复用缓存。
    void prepare(const NoteRenderCache& notes, qreal devicePixelRatio);
    // 以样式与量化宽度为键绘制长方形体，拆分首/中/尾三段保持边界质量。
    void drawBody(QPainter& painter, const QRectF& rect,
                  const NoteRenderStyle& style, int styleIndex);

private:
    QCache<quint64, QImage> m_images {16 * 1024}; // 以 KiB 计费，限制缓存约 16 MiB。
    quint64 m_chartRevision = 0;
    quint64 m_geometryRevision = 0;
    quint64 m_materialRevision = 0;
    qreal m_dpr = 0;
};

} // namespace midi_play::presentation::visualization
