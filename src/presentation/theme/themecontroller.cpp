#include "themecontroller.h"

#include <QApplication>
#include <QStyleHints>

namespace midi_play::presentation::theme {

ThemeController::ThemeController(midi_play::settings::ThemeMode mode, QObject* parent)
    : QObject(parent), m_mode(midi_play::settings::normalizeThemeMode(mode))
{
    // Apply even the default mode before the first window is constructed.
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
