#include "customtitlebar.h"

#include <QEvent>
#include <QMouseEvent>
#include <QWindow>

namespace midi_play::presentation::windowchrome {

CustomTitleBar::CustomTitleBar(QWidget* parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_StyledBackground, true);
    setMouseTracking(true);
    installEventFilter(this);
}

void CustomTitleBar::registerDragWidget(QWidget* widget)
{
    if (!widget || widget == this) {
        return;
    }
    widget->installEventFilter(this);
    widget->setMouseTracking(true);
}

void CustomTitleBar::mousePressEvent(QMouseEvent* event)
{
    if (!handleMousePress(event)) {
        QWidget::mousePressEvent(event);
    }
}

void CustomTitleBar::mouseDoubleClickEvent(QMouseEvent* event)
{
    if (!handleMouseDoubleClick(event)) {
        QWidget::mouseDoubleClickEvent(event);
    }
}

bool CustomTitleBar::eventFilter(QObject* watched, QEvent* event)
{
    Q_UNUSED(watched)
    if (event->type() == QEvent::MouseButtonPress
        && handleMousePress(static_cast<QMouseEvent*>(event))) {
        return true;
    }
    if (event->type() == QEvent::MouseButtonDblClick
        && handleMouseDoubleClick(static_cast<QMouseEvent*>(event))) {
        return true;
    }
    return QWidget::eventFilter(watched, event);
}

bool CustomTitleBar::handleMousePress(QMouseEvent* event)
{
    if (!event || event->button() != Qt::LeftButton) {
        return false;
    }
    auto* widgetWindow = QWidget::window();
    auto* nativeWindow = widgetWindow ? widgetWindow->windowHandle() : nullptr;
    if (!nativeWindow || widgetWindow->isFullScreen()) {
        return false;
    }
    if (nativeWindow->startSystemMove()) {
        event->accept();
        return true;
    }
    return false;
}

bool CustomTitleBar::handleMouseDoubleClick(QMouseEvent* event)
{
    if (!event || event->button() != Qt::LeftButton) {
        return false;
    }
    toggleMaximized();
    event->accept();
    return true;
}

void CustomTitleBar::toggleMaximized()
{
    if (auto* window = this->window()) {
        if (window->isMaximized()) {
            window->showNormal();
        } else {
            window->showMaximized();
        }
    }
}

} // namespace midi_play::presentation::windowchrome
