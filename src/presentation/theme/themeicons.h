#pragma once
// 随主题和控件状态变化的内置命令图标接口。

#include "apptheme.h"
#include <QIcon>

namespace midi_play::presentation::theme {

// 主窗口、传输控制及标题栏使用的固定图形类别。
enum class IconGlyph { Minimize, Maximize, Restore, Close, Play, Pause, Stop, Open, Export, Settings };
// 创建持有主题颜色副本的 QIcon；主题切换后调用方应重新生成图标。
QIcon themedIcon(IconGlyph glyph, const AppTheme& theme);

} // namespace midi_play::presentation::theme
