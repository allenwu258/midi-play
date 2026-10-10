// QIconEngine 图标实现：按目标尺寸缩放固定逻辑图形，并匹配禁用/悬停颜色。
#include "themeicons.h"

#include <QIconEngine>
#include <QPainter>
#include <QPixmap>

namespace midi_play::presentation::theme {
namespace {

// 引擎保存颜色副本，避免 QIcon 的延后绘制借用已变更的主题对象。
class ThemeIconEngine final : public QIconEngine {
public:
    ThemeIconEngine(IconGlyph glyph, const WidgetColors& colors) : m_glyph(glyph), m_colors(colors) {}
    // 满足 Qt 图标隐式共享时的引擎复制契约。
    QIconEngine* clone() const override { return new ThemeIconEngine(*this); }

    // 传输图标使用 26 单位填充图形，其他图标使用 18 单位线描。
    void paint(QPainter* painter, const QRect& rect, QIcon::Mode mode, QIcon::State) override
    {
        const bool transport = m_glyph == IconGlyph::Play || m_glyph == IconGlyph::Pause || m_glyph == IconGlyph::Stop;
        const qreal size = transport ? 26 : 18;
        QColor color = transport ? m_colors.onAccent : m_colors.buttonText;
        if (mode == QIcon::Disabled) color = m_colors.iconDisabled;
        else if (m_glyph == IconGlyph::Close && mode == QIcon::Active) color = m_colors.onAccent;
        painter->save();
        painter->translate(rect.topLeft());
        painter->scale(rect.width() / size, rect.height() / size);
        painter->setRenderHint(QPainter::Antialiasing, true);
        painter->setPen(QPen(color, 1.7, Qt::SolidLine, Qt::SquareCap, Qt::MiterJoin));
        painter->setBrush(Qt::NoBrush);
        if (transport) {
            painter->setPen(Qt::NoPen);
            painter->setBrush(color);
        }
        switch (m_glyph) {
        case IconGlyph::Minimize: painter->drawLine(QPointF(3, 11), QPointF(14, 11)); break;
        case IconGlyph::Maximize: painter->drawRect(QRectF(3, 3, 11, 11)); break;
        case IconGlyph::Restore:
            painter->drawRect(QRectF(5, 2, 10, 10));
            painter->drawPolyline(QPolygonF {QPointF(5, 6), QPointF(3, 6), QPointF(3, 15), QPointF(12, 15)});
            break;
        case IconGlyph::Close:
            painter->drawLine(QPointF(4, 4), QPointF(13, 13));
            painter->drawLine(QPointF(13, 4), QPointF(4, 13));
            break;
        case IconGlyph::Play:
            painter->drawPolygon(QPolygonF {QPointF(6, 3), QPointF(22, 13), QPointF(6, 23)});
            break;
        case IconGlyph::Pause:
            painter->drawRoundedRect(QRectF(5, 3, 6, 20), 1.5, 1.5);
            painter->drawRoundedRect(QRectF(15, 3, 6, 20), 1.5, 1.5);
            break;
        case IconGlyph::Stop: painter->drawRoundedRect(QRectF(4, 4, 18, 18), 1.5, 1.5); break;
        case IconGlyph::Open:
            painter->drawPolyline(QPolygonF {QPointF(2, 14), QPointF(2, 4), QPointF(7, 4), QPointF(9, 6), QPointF(15, 6)});
            painter->drawPolygon(QPolygonF {QPointF(2, 14), QPointF(5, 8), QPointF(16, 8), QPointF(13, 14)});
            break;
        case IconGlyph::Export:
            painter->drawLine(QPointF(9, 2), QPointF(9, 11));
            painter->drawPolyline(QPolygonF {QPointF(5, 8), QPointF(9, 12), QPointF(13, 8)});
            painter->drawPolyline(QPolygonF {QPointF(3, 12), QPointF(3, 16), QPointF(15, 16), QPointF(15, 12)});
            break;
        case IconGlyph::Settings:
            for (int y : {4, 9, 14}) painter->drawLine(QPointF(2, y), QPointF(16, y));
            painter->setBrush(color);
            for (const QPointF point : {QPointF(6, 4), QPointF(12, 9), QPointF(7, 14)})
                painter->drawEllipse(point, 1.7, 1.7);
            break;
        }
        painter->restore();
    }

    // 在透明位图上复用同一 paint 路径，保持模式和尺寸行为一致。
    QPixmap pixmap(const QSize& size, QIcon::Mode mode, QIcon::State state) override
    {
        QPixmap result(size);
        result.fill(Qt::transparent);
        QPainter painter(&result);
        paint(&painter, QRect(QPoint(), size), mode, state);
        return result;
    }

private:
    IconGlyph m_glyph;
    WidgetColors m_colors;
};

} // namespace

// QIcon 接管新引擎所有权；引擎保存色表副本，主题切换时由调用方重新创建图标。
QIcon themedIcon(IconGlyph glyph, const AppTheme& theme)
{
    return QIcon(new ThemeIconEngine(glyph, theme.widgets));
}

} // namespace midi_play::presentation::theme
