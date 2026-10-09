#pragma once

// 标题栏系统按钮：提供最小化、最大化/还原及关闭的 Qt 绘制和无障碍入口。

#include "domain/settings/thememode.h"

#include <QWidget>
#include <array>

class QToolButton;

namespace midi_play::presentation::windowchrome {

// 顺序与内部三个按钮数组的索引一致。
enum class CaptionCommand { Minimize, MaximizeOrRestore, Close };

// Qt 负责绘制和无障碍；WindowChromeController 桥接非客户区输入，本控件不持有原生句柄。
class CaptionButtons final : public QWidget {
    Q_OBJECT
public:
    explicit CaptionButtons(QWidget* parent = nullptr);
    // 返回有效命令对应的按钮，供原生命中测试与输入桥接使用。
    QToolButton* button(CaptionCommand command) const noexcept;
    // 设置逻辑像素尺寸，将总宽度均分并分配余数，确保三按钮总宽度精确一致。
    void setMetrics(int totalWidth, int height);
    // 同步主题色并请求重绘。
    void setTheme(midi_play::settings::ThemeMode theme);
    // 切换最大化/还原图形，同时更新工具提示和无障碍名称。
    void setMaximized(bool maximized);
    // 同步窗口激活状态，用于区分普通与弱化的前景色。
    void setWindowActive(bool active);
    // 原生输入指定唯一悬停或按下按钮，空指针清除对应状态。
    void setNativeHovered(QToolButton* button);
    void setNativePressed(QToolButton* button);
    // 系统高对比度启用时使用传入系统色，普通模式使用应用主题。
    void setSystemColors(bool highContrast, const QColor& text,
                         const QColor& background, const QColor& highlight,
                         const QColor& highlightedText);

signals:
    // 统一用户点击和原生输入桥接产生的命令，由窗口装饰控制器执行。
    void commandRequested(midi_play::presentation::windowchrome::CaptionCommand command);

private:
    std::array<QToolButton*, 3> m_buttons;
};

} // namespace midi_play::presentation::windowchrome
