#pragma once
// 命名主题颜色到 Qt 样式表的适配接口，布局规则由固定模板保持。

#include "apptheme.h"
#include <QString>

namespace midi_play::presentation::theme {

// 主窗口工具条、传输区和导出标签页样式。
QString mainWindowStyle(const AppTheme& theme);
// 设置及导出对话框通用表单、输入和状态样式。
QString settingsDialogStyle(const AppTheme& theme);
// 倍率按钮和独立弹窗样式。
QString playbackRateStyle(const AppTheme& theme);

} // namespace midi_play::presentation::theme
