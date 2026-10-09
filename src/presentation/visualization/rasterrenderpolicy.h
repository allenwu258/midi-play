#pragma once

// QWidget 栅格绘制提示的统一策略。
class QPainter;

namespace midi_play::presentation::visualization {

// 轴对齐几何使用非抗锯齿路径控制开销，文字保留独立抗锯齿以维持可读性。
class RasterRenderPolicy final {
public:
    // 应用几何、图像缩放和文字的固定 render hint 组合。
    static void apply(QPainter& painter);
};

} // namespace midi_play::presentation::visualization
