#pragma once

#include "visualization/introtext.h"
#include <QWidget>

namespace midi_play::presentation {

class IntroTextPreviewWidget final : public QWidget {
    Q_OBJECT
public:
    explicit IntroTextPreviewWidget(QWidget* parent = nullptr);
    QSize sizeHint() const override { return {512, 288}; }
    int heightForWidth(int width) const override { return qMin(288, width * 9 / 16); }
    void setBaseFrame(const QImage& frame, qint64 outputTimeUs);
    QString setConfig(const app::IntroTextConfig& config);
    void setLayoutMode(bool enabled);
    QRectF logicalBounds() const { return m_asset.logicalBounds(); }

signals:
    void positionDragged(int offsetX, int offsetY);

protected:
    bool event(QEvent* event) override;
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    QRectF canvasRect() const;
    QPointF toLogicalPoint(QPointF point) const;
    void moveBlockTo(QPointF center);
    void finishDrag();

    QImage m_baseFrame;
    app::IntroTextConfig m_config;
    visualization::IntroTextAsset m_asset;
    qint64 m_outputTimeUs = 0;
    bool m_layoutMode = true;
    bool m_dragging = false;
    QPointF m_dragStart;
    QPointF m_dragCenter;
};
} // namespace midi_play::presentation
