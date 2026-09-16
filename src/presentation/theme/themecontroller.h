#pragma once

#include "apptheme.h"
#include <QObject>

namespace midi_play::presentation::theme {

// GUI-thread application appearance. Persistence belongs to SettingsService.
class ThemeController final : public QObject {
    Q_OBJECT
public:
    explicit ThemeController(midi_play::settings::ThemeMode mode = midi_play::settings::kDefaultThemeMode,
                             QObject* parent = nullptr);
    midi_play::settings::ThemeMode mode() const noexcept { return m_mode; }

public slots:
    void setMode(midi_play::settings::ThemeMode mode);

signals:
    void themeChanged(midi_play::settings::ThemeMode mode);

private:
    void applyApplicationPalette();
    midi_play::settings::ThemeMode m_mode;
};

} // namespace midi_play::presentation::theme
