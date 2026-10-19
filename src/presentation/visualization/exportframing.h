#pragma once

// 纯 CPU 导出画幅规划：先约束逻辑布局，再映射到最终分辨率。
#include "scenelayoutengine.h"

#include <QPointF>
#include <QRect>
#include <QSize>

namespace midi_play::presentation::visualization {

struct ExportFramingPlan {
    QSize logicalContentSize;
    QSize logicalSceneSize;
    QRectF logicalCropRect;
    SceneLayoutConstraints layoutConstraints;
    qreal scale = 1.0; // 逻辑坐标到最终 framebuffer 像素的变换，不缩放已渲染图像。
    QPointF offset;
    QRect scissorRect;
};

// 保留标记栏时使用完整逻辑画布；裁栏时向左扩展场景，保证 crop 大小不变。
// 不支持的画幅或无法满足的布局抛出 std::invalid_argument / std::runtime_error。
ExportFramingPlan calculateExportFramingPlan(
    const QSize& logicalContentSize, const QSize& outputSize,
    const midi_play::visualization::VisualChart* chart, qint64 lookAheadUs,
    bool showNotationStrip, bool retainMeasureGutter);

bool isWideExportFrame(const QSize& size);

} // namespace midi_play::presentation::visualization
