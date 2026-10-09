#pragma once
// 应用级主题控制器，同步 Qt 调色板及控件外观更新信号。

#include "apptheme.h"
#include <QObject>

namespace midi_play::presentation::theme {

// 仅在 GUI 线程操作应用外观，持久化由 SettingsService 负责。
class ThemeController final : public QObject {
    Q_OBJECT
public:
    // 构造时立即应用规范化主题，应先于主窗口创建。
    explicit ThemeController(midi_play::settings::ThemeMode mode = midi_play::settings::kDefaultThemeMode,
                             QObject* parent = nullptr);
    // 返回当前已应用模式。
    midi_play::settings::ThemeMode mode() const noexcept { return m_mode; }

public slots:
    // 模式实际变化时先应用全局调色板，再通知各窗口更新局部样式。
    void setMode(midi_play::settings::ThemeMode mode);

signals:
    // 通知重新生成 QSS、图标及渲染主题。
    void themeChanged(midi_play::settings::ThemeMode mode);

private:
    // 同时设置 Qt 色彩方案提示及 QApplication 调色板。
    void applyApplicationPalette();
    midi_play::settings::ThemeMode m_mode;
};

} // namespace midi_play::presentation::theme
