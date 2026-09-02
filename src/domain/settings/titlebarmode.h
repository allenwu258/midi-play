#pragma once

#include <QtGlobal>
#include <QMetaType>

namespace midi_play::settings {

enum class TitleBarMode : int {
    Native = 0,
    Custom = 1,
};

#if defined(Q_OS_WIN)
inline constexpr bool kCustomTitleBarAvailable = true;
// Keep the stable, OS-managed chrome as the Windows default. The custom
// implementation remains available as an explicit experimental opt-in.
inline constexpr TitleBarMode kDefaultTitleBarMode = TitleBarMode::Native;
#else
inline constexpr bool kCustomTitleBarAvailable = false;
inline constexpr TitleBarMode kDefaultTitleBarMode = TitleBarMode::Native;
#endif

constexpr bool isCustomTitleBarAvailable() noexcept
{
    return kCustomTitleBarAvailable;
}

constexpr bool isValidTitleBarMode(TitleBarMode mode) noexcept
{
    return mode == TitleBarMode::Native
        || (mode == TitleBarMode::Custom && kCustomTitleBarAvailable);
}

constexpr TitleBarMode normalizeTitleBarMode(TitleBarMode mode) noexcept
{
    return isValidTitleBarMode(mode) ? mode : TitleBarMode::Native;
}

constexpr int titleBarModePersistentValue(TitleBarMode mode) noexcept
{
    return static_cast<int>(normalizeTitleBarMode(mode));
}

constexpr TitleBarMode titleBarModeFromPersistentValue(int value) noexcept
{
    if (value == static_cast<int>(TitleBarMode::Custom) && kCustomTitleBarAvailable) {
        return TitleBarMode::Custom;
    }
    return TitleBarMode::Native;
}

} // namespace midi_play::settings

Q_DECLARE_METATYPE(midi_play::settings::TitleBarMode)
