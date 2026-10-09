#pragma once

// 将背景适配偏好转换为目标矩形和源纹理 UV，统一 Qt/Vulkan 取样范围。
#include "domain/settings/backgroundimagealignment.h"

#include <QRectF>
#include <QSizeF>

namespace midi_play::presentation::visualization {

// destination 使用逻辑像素，sourceUv 使用源图归一化坐标。
struct BackgroundImagePlacement {
    QRectF destination;
    // 源矩形分量限制到 [0, 1]。
    QRectF sourceUv;
};

// Contain 居中完整显示，其他模式填满并按对齐方向裁剪；非法尺寸返回空结果。
BackgroundImagePlacement calculateBackgroundImagePlacement(
    QSizeF sourceSize, QRectF targetRect,
    midi_play::settings::BackgroundImageAlignment alignment);

} // namespace midi_play::presentation::visualization
