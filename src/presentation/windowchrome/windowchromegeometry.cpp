// 窗口区域命中测试实现：独立于 QWidget 和 Win32 消息，统一各类区域的优先级。
#include "windowchromegeometry.h"

namespace midi_play::presentation::windowchrome {

FrameHit hitTestFrame(const FrameGeometry& g, QPointF point) noexcept
{
    if (g.fullScreen || !g.window.contains(point)) return FrameHit::Client;

    // 最大化时禁用边框缩放；普通窗口按允许的轴判断，边角优先于单边。
    if (!g.maximized) {
        const bool left = g.resizeHorizontal && point.x() < g.window.left() + g.resizeBorder.left();
        const bool right = g.resizeHorizontal && point.x() >= g.window.right() - g.resizeBorder.right();
        const bool top = g.resizeVertical && point.y() < g.window.top() + g.resizeBorder.top();
        const bool bottom = g.resizeVertical && point.y() >= g.window.bottom() - g.resizeBorder.bottom();
        if (top && left) return FrameHit::TopLeft;
        if (top && right) return FrameHit::TopRight;
        if (bottom && left) return FrameHit::BottomLeft;
        if (bottom && right) return FrameHit::BottomRight;
        if (left) return FrameHit::Left;
        if (right) return FrameHit::Right;
        if (top) return FrameHit::Top;
        if (bottom) return FrameHit::Bottom;
    }

    // 系统菜单保留独立命中类型，交互控件排除后剩余标题区域才可拖动。
    if (!g.systemMenu.isEmpty() && g.systemMenu.contains(point)) return FrameHit::SystemMenu;
    for (const QRectF& region : g.interactive) {
        if (region.contains(point)) return FrameHit::Client;
    }
    return g.caption.contains(point) ? FrameHit::Caption : FrameHit::Client;
}

} // namespace midi_play::presentation::windowchrome
