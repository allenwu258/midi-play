// 扩展区只在初始化时求值一次；离散虚拟键数量不参与反复求根。
#include "exportframing.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace midi_play::presentation::visualization {

bool isWideExportFrame(const QSize& size)
{
    return size.width() > 0 && size.height() > 0
        && qint64(size.width()) * 9 == qint64(size.height()) * 16;
}

ExportFramingPlan calculateExportFramingPlan(
    const QSize& logicalContentSize, const QSize& outputSize,
    const midi_play::visualization::VisualChart* chart, qint64 lookAheadUs,
    bool showNotationStrip, bool retainMeasureGutter)
{
    if (!isWideExportFrame(logicalContentSize) || !isWideExportFrame(outputSize)) {
        throw std::invalid_argument("Video export requires a 16:9 logical canvas and output frame");
    }
    ExportFramingPlan plan;
    plan.logicalContentSize = logicalContentSize;
    plan.logicalSceneSize = logicalContentSize;
    qreal cropLeft = 0.0;
    if (!retainMeasureGutter) {
        const SceneLayoutEngine engine;
        const auto baseline = engine.layout(logicalContentSize, chart, lookAheadUs, showNotationStrip);
        const qreal baselineLeft = baseline.pianoRect.isEmpty()
            ? baseline.drumRect.left() : baseline.pianoRect.left();
        // 沿用当前布局需要的完整虚拟键数；向上取整只发生在被裁掉的区域。
        // 同时锁定宽度和键数，避免扩宽视口后自动补键发生离散跳变。
        const int whiteKeys = int(std::count_if(baseline.leftKeyboardExtensionPitches.cbegin(),
            baseline.leftKeyboardExtensionPitches.cend(), [](const auto& slot) { return !slot.blackKey; }));
        cropLeft = std::ceil(baselineLeft);
        plan.logicalSceneSize.rwidth() += int(cropLeft);
        plan.layoutConstraints.leftExtension = LeftExtensionConstraint {cropLeft, whiteKeys};
        const auto geometry = engine.layout(plan.logicalSceneSize, chart, lookAheadUs,
                                             showNotationStrip, plan.layoutConstraints);
        const qreal left = geometry.pianoRect.isEmpty() ? geometry.drumRect.left() : geometry.pianoRect.left();
        const qreal right = geometry.drumRect.isEmpty() ? geometry.pianoRect.right() : geometry.drumRect.right();
        constexpr qreal tolerance = 0.000001;
        if (std::abs(left - cropLeft) > tolerance
            || std::abs(right - left - logicalContentSize.width()) > tolerance
            || std::abs(geometry.keyboardRect.bottom() - logicalContentSize.height()) > tolerance) {
            throw std::runtime_error("Export layout does not match the requested content frame");
        }
    }
    plan.logicalCropRect = QRectF(QPointF(cropLeft, 0.0), QSizeF(logicalContentSize));
    plan.scale = qreal(outputSize.width()) / logicalContentSize.width();
    plan.offset = QPointF(-cropLeft * plan.scale, 0.0);
    plan.scissorRect = QRect(QPoint(), outputSize);
    return plan;
}

} // namespace midi_play::presentation::visualization
