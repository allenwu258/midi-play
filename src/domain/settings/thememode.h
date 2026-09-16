#pragma once

#include <QMetaType>

namespace midi_play::settings {

enum class ThemeMode : int { Dark = 0, Light = 1 };
inline constexpr ThemeMode kDefaultThemeMode = ThemeMode::Dark;

inline constexpr bool isValidThemeMode(int value) noexcept
{
    return value == int(ThemeMode::Dark) || value == int(ThemeMode::Light);
}

inline constexpr ThemeMode normalizeThemeMode(ThemeMode mode) noexcept
{
    return mode == ThemeMode::Light ? ThemeMode::Light : kDefaultThemeMode;
}

inline constexpr int themeModePersistentValue(ThemeMode mode) noexcept
{
    return int(normalizeThemeMode(mode));
}

inline constexpr ThemeMode themeModeFromPersistentValue(int value) noexcept
{
    return normalizeThemeMode(static_cast<ThemeMode>(value));
}

} // namespace midi_play::settings

Q_DECLARE_METATYPE(midi_play::settings::ThemeMode)
