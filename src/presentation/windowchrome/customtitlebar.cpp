#include "customtitlebar.h"

#include <QStyle>
#include <QVariant>

namespace midi_play::presentation::windowchrome {

CustomTitleBar::CustomTitleBar(QWidget* parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_StyledBackground, true);
    setMouseTracking(true);
    setProperty("windowActive", true);
}

void CustomTitleBar::setWindowActive(bool active)
{
    if (property("windowActive").toBool() == active) return;
    setProperty("windowActive", active);
    for (auto* widget : findChildren<QWidget*>()) {
        widget->style()->unpolish(widget);
        widget->style()->polish(widget);
        widget->update();
    }
}

} // namespace midi_play::presentation::windowchrome
