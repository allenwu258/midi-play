#pragma once

#include <QWidget>

class QMouseEvent;

namespace midi_play::presentation::windowchrome {

class CustomTitleBar final : public QWidget {
    Q_OBJECT
public:
    explicit CustomTitleBar(QWidget* parent = nullptr);

    void registerDragWidget(QWidget* widget);
    void setDragEnabled(bool enabled) noexcept { m_dragEnabled = enabled; }
    bool isDragEnabled() const noexcept { return m_dragEnabled; }

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    bool handleMousePress(QMouseEvent* event);
    bool handleMouseDoubleClick(QMouseEvent* event);
    void toggleMaximized();

    bool m_dragEnabled = false;
};

} // namespace midi_play::presentation::windowchrome
