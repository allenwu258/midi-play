#pragma once

// 窗口装饰几何模型：以窗口相对物理像素描述缩放边框、标题区和交互区域。

#include <QMargins>
#include <QPointF>
#include <QRectF>
#include <QVector>

namespace midi_play::presentation::windowchrome {

// 平台无关命中类型，由原生控制器映射到系统命中测试返回值。
enum class FrameHit {
    Client, Caption, SystemMenu,
    Left, Right, Top, Bottom, TopLeft, TopRight, BottomLeft, BottomRight,
};

// 所有区域均为窗口相对物理像素；标题栏系统按钮由控制器先行检测。
struct FrameGeometry {
    QRectF window;
    QRectF caption;
    QRectF systemMenu;
    // 交互控件区域返回客户区，防止按钮点击触发窗口拖动。
    QVector<QRectF> interactive;
    QMargins resizeBorder;
    bool resizeHorizontal = true;
    bool resizeVertical = true;
    bool maximized = false;
    bool fullScreen = false;
};

// 按边角、边缘、系统菜单、交互控件、标题区的顺序检测；全屏及窗口外返回客户区。
FrameHit hitTestFrame(const FrameGeometry& geometry, QPointF point) noexcept;

} // namespace midi_play::presentation::windowchrome
