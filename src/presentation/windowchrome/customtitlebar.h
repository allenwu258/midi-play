#pragma once

#include <QWidget>

class QMouseEvent;

namespace midi_play::presentation::windowchrome {

class CustomTitleBar final : public QWidget {
    Q_OBJECT
public:
    explicit CustomTitleBar(QWidget* parent = nullptr);

    void registerDragWidget(QWidget* widget);

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    bool handleMousePress(QMouseEvent* event);
    bool handleMouseDoubleClick(QMouseEvent* event);
    void toggleMaximized();
};

} // namespace midi_play::presentation::windowchrome
