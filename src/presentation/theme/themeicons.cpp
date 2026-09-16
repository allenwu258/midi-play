#include "themeicons.h"

#include <QIconEngine>
#include <QPainter>
#include <QPixmap>

namespace midi_play::presentation::theme {
namespace {

class ThemeIconEngine final : public QIconEngine {
public:
    ThemeIconEngine(IconGlyph glyph, const WidgetColors& colors) : m_glyph(glyph), m_colors(colors) {}
    QIconEngine* clone() const override { return new ThemeIconEngine(*this); }

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
        case IconGlyph::Settings:
            for (int y : {4, 9, 14}) painter->drawLine(QPointF(2, y), QPointF(16, y));
            painter->setBrush(color);
            for (const QPointF point : {QPointF(6, 4), QPointF(12, 9), QPointF(7, 14)})
                painter->drawEllipse(point, 1.7, 1.7);
            break;
        }
        painter->restore();
    }

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

QIcon themedIcon(IconGlyph glyph, const AppTheme& theme)
{
    return QIcon(new ThemeIconEngine(glyph, theme.widgets));
}

} // namespace midi_play::presentation::theme
