#pragma once

// 轨道色相与 OKLCH 到显示色域的转换接口。
#include "domain/visualization/visualchart.h"

#include <QColor>

namespace midi_play::presentation::visualization {

// 色相角使用弧度，输出为未预乘 alpha 的 sRGB；通过降低彩度进入色域。
// 从轨道基础颜色取得 OKLab 色相。
double noteColorHue(const midi_play::visualization::ColorRgba& color);
// 将 OKLCH 转为 QColor，保持色相并迭代降低越界彩度。
QColor noteColorFromOklch(double lightness, double chroma, double angle, double alpha);
// 鲜明模式按轨道近似色族及音级/鼓类生成稳定色相。
double vividNoteHue(int trackIndex, int pitch, bool percussion);

} // namespace midi_play::presentation::visualization
