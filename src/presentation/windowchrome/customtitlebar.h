#pragma once

// 自定义顶栏外观容器：通过动态属性区分窗口的激活和非激活状态。

#include <QWidget>

namespace midi_play::presentation::windowchrome {

// 只管理样式状态；拖动、缩放和系统菜单交互由 WindowChromeController 实现。
class CustomTitleBar final : public QWidget {
public:
    explicit CustomTitleBar(QWidget* parent = nullptr);
    // 状态变化后重新应用子控件样式，使属性选择器立即反映窗口激活状态。
    void setWindowActive(bool active);
};

} // namespace midi_play::presentation::windowchrome
