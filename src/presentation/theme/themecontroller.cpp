// 全局主题应用实现，避免首窗口出现时短暂使用另一套调色板。
#include "themecontroller.h"

#include <QApplication>
#include <QStyleHints>

namespace midi_play::presentation::theme {

ThemeController::ThemeController(midi_play::settings::ThemeMode mode, QObject* parent)
    : QObject(parent), m_mode(midi_play::settings::normalizeThemeMode(mode))
{
    // 即使采用默认主题，也应在首窗口构造前完成全局外观设置。
    applyApplicationPalette();
}

void ThemeController::setMode(midi_play::settings::ThemeMode mode)
{
    const auto normalized = midi_play::settings::normalizeThemeMode(mode);
    if (m_mode == normalized) return;
    m_mode = normalized;
    applyApplicationPalette();
    emit themeChanged(m_mode);
}

void ThemeController::applyApplicationPalette()
{
    QGuiApplication::styleHints()->setColorScheme(m_mode == midi_play::settings::ThemeMode::Light
        ? Qt::ColorScheme::Light : Qt::ColorScheme::Dark);
    QApplication::setPalette(widgetPalette(themeFor(m_mode)));
}

} // namespace midi_play::presentation::theme
