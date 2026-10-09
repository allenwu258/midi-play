// 顶栏外观实现：启用样式背景，并在激活状态改变后刷新后代控件样式。
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
    // 样式表包含祖先属性条件，需要重新 polish 子控件以重新求值。
    for (auto* widget : findChildren<QWidget*>()) {
        widget->style()->unpolish(widget);
        widget->style()->polish(widget);
        widget->update();
    }
}

} // namespace midi_play::presentation::windowchrome
