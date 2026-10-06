#pragma once

#include <QMargins>
#include <QPointF>
#include <QRectF>
#include <QVector>

namespace midi_play::presentation::windowchrome {

enum class FrameHit {
    Client, Caption, SystemMenu,
    Left, Right, Top, Bottom, TopLeft, TopRight, BottomLeft, BottomRight,
};

// All coordinates are window-relative physical pixels. Caption controls are
// tested by the controller first; this function handles the remaining regions.
struct FrameGeometry {
    QRectF window;
    QRectF caption;
    QRectF systemMenu;
    QVector<QRectF> interactive;
    QMargins resizeBorder;
    bool resizeHorizontal = true;
    bool resizeVertical = true;
    bool maximized = false;
    bool fullScreen = false;
};

FrameHit hitTestFrame(const FrameGeometry& geometry, QPointF point) noexcept;

} // namespace midi_play::presentation::windowchrome
