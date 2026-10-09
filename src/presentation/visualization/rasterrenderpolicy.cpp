#include "rasterrenderpolicy.h"

// 实现栅格后端默认绘制提示，局部绘制仍可显式覆盖。
#include <QPainter>

namespace midi_play::presentation::visualization {

// 不改变调用方的变换和画刷，仅配置三项绘制提示。
void RasterRenderPolicy::apply(QPainter& painter)
{
    painter.setRenderHint(QPainter::Antialiasing, false);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
    painter.setRenderHint(QPainter::TextAntialiasing, true);
}

} // namespace midi_play::presentation::visualization
