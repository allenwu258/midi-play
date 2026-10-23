#include "introtextpreviewwidget.h"
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <algorithm>
#include <cmath>

namespace midi_play::presentation {
IntroTextPreviewWidget::IntroTextPreviewWidget(QWidget* parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("videoExportPreview"));
    setAccessibleName(QStringLiteral("视频预览画布"));
    setMinimumSize(256, 144);
    setMaximumSize(512, 288);
    QSizePolicy policy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    policy.setHeightForWidth(true);
    setSizePolicy(policy);
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setToolTip(QStringLiteral("布局模式：拖动文字块；方向键移动 1 px，Shift + 方向键移动 10 px。"));
}

void IntroTextPreviewWidget::setBaseFrame(const QImage& frame, qint64 outputTimeUs)
{
    const bool sizeChanged = frame.size() != m_asset.outputSize();
    m_baseFrame = frame;
    m_outputTimeUs = outputTimeUs;
    if (sizeChanged && m_config.enabled) setConfig(m_config);
    update();
}

QString IntroTextPreviewWidget::setConfig(const app::IntroTextConfig& config)
{
    m_config = config;
    QString error;
    m_asset.prepare(config, m_baseFrame.isNull() ? QSize(1024, 576) : m_baseFrame.size(), &error);
    if (!config.enabled || !m_asset.isValid()) finishDrag();
    update();
    return error;
}

void IntroTextPreviewWidget::setLayoutMode(bool enabled)
{
    m_layoutMode = enabled;
    if (!enabled) finishDrag();
    update();
}

QRectF IntroTextPreviewWidget::canvasRect() const
{
    const qreal scale = std::min(width() / 1280.0, height() / 720.0);
    return {(width() - 1280 * scale) / 2, (height() - 720 * scale) / 2, 1280 * scale, 720 * scale};
}

QPointF IntroTextPreviewWidget::toLogicalPoint(QPointF point) const
{
    const QRectF canvas = canvasRect();
    return (point - canvas.topLeft()) * (1280.0 / canvas.width());
}

void IntroTextPreviewWidget::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.fillRect(rect(), QColor(18, 20, 22));
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    const QRectF canvas = canvasRect();
    if (!m_baseFrame.isNull()) painter.drawImage(canvas, m_baseFrame);
    if (!m_config.enabled || !m_asset.isValid()) return;
    painter.save();
    painter.translate(canvas.topLeft());
    painter.scale(canvas.width() / m_asset.outputSize().width(),
                  canvas.height() / m_asset.outputSize().height());
    m_asset.paint(painter, m_outputTimeUs, m_layoutMode);
    painter.restore();
    if (m_layoutMode) {
        const qreal scale = canvas.width() / 1280.0;
        painter.setPen(QPen(QColor(85, 201, 164), 1.0, Qt::DashLine));
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(QRectF(canvas.topLeft() + m_asset.logicalBounds().topLeft() * scale,
                                m_asset.logicalBounds().size() * scale));
    }
}

void IntroTextPreviewWidget::mousePressEvent(QMouseEvent* event)
{
    const QPointF logical = toLogicalPoint(event->position());
    if (event->button() != Qt::LeftButton || !m_layoutMode || !m_asset.isValid()
        || !m_asset.logicalBounds().contains(logical)) {
        QWidget::mousePressEvent(event);
        return;
    }
    setFocus(Qt::MouseFocusReason);
    m_dragging = true;
    m_dragStart = logical;
    m_dragCenter = m_asset.logicalBounds().center();
    setCursor(Qt::ClosedHandCursor);
    grabMouse();
    event->accept();
}

void IntroTextPreviewWidget::moveBlockTo(QPointF center)
{
    const QSizeF size = m_asset.logicalBounds().size();
    // Clamp integer offsets, not fractional edges; rounding must never create a 0.5 px overflow.
    const int minX = int(std::ceil(size.width() / 2.0 - 640));
    const int maxX = int(std::floor(640 - size.width() / 2.0));
    const int minY = int(std::ceil(size.height() / 2.0 - 360));
    const int maxY = int(std::floor(360 - size.height() / 2.0));
    if (minX > maxX || minY > maxY) return;
    emit positionDragged(std::clamp(qRound(center.x() - 640), minX, maxX),
                         std::clamp(qRound(center.y() - 360), minY, maxY));
}

void IntroTextPreviewWidget::mouseMoveEvent(QMouseEvent* event)
{
    const QPointF logical = toLogicalPoint(event->position());
    if (m_dragging) {
        moveBlockTo(m_dragCenter + logical - m_dragStart);
        event->accept();
    } else {
        setCursor(m_layoutMode && m_asset.isValid() && m_asset.logicalBounds().contains(logical)
                      ? Qt::OpenHandCursor : Qt::ArrowCursor);
        QWidget::mouseMoveEvent(event);
    }
}

void IntroTextPreviewWidget::finishDrag()
{
    if (m_dragging) {
        m_dragging = false;
        releaseMouse();
    }
    unsetCursor();
}

void IntroTextPreviewWidget::mouseReleaseEvent(QMouseEvent* event)
{
    if (m_dragging && event->button() == Qt::LeftButton) {
        moveBlockTo(m_dragCenter + toLogicalPoint(event->position()) - m_dragStart);
        finishDrag();
        event->accept();
    } else QWidget::mouseReleaseEvent(event);
}

void IntroTextPreviewWidget::keyPressEvent(QKeyEvent* event)
{
    if (!m_layoutMode || !m_asset.isValid()) {
        QWidget::keyPressEvent(event);
        return;
    }
    const int step = event->modifiers().testFlag(Qt::ShiftModifier) ? 10 : 1;
    QPointF delta;
    switch (event->key()) {
    case Qt::Key_Left: delta.setX(-step); break;
    case Qt::Key_Right: delta.setX(step); break;
    case Qt::Key_Up: delta.setY(-step); break;
    case Qt::Key_Down: delta.setY(step); break;
    default: QWidget::keyPressEvent(event); return;
    }
    moveBlockTo(m_asset.logicalBounds().center() + delta);
    event->accept();
}
} // namespace midi_play::presentation
