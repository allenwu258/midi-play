#pragma once
#include <QMetaType>

namespace midi_play::settings {
enum class BackgroundMode : int { None = 0, Image = 1, Video = 2 };
inline constexpr BackgroundMode normalizeBackgroundMode(BackgroundMode mode) noexcept
{
    return mode >= BackgroundMode::None && mode <= BackgroundMode::Video ? mode : BackgroundMode::None;
}
}
Q_DECLARE_METATYPE(midi_play::settings::BackgroundMode)
