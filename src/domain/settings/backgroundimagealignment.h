#pragma once

#include <QMetaType>

namespace midi_play::settings {

// Placement modes are intentionally shared by the traditional and Vulkan
// renderers so a saved preference has identical meaning in both backends.
enum class BackgroundImageAlignment : int {
    Top = 0,
    Bottom = 1,
    Left = 2,
    Right = 3,
    Cover = 4,
    Contain = 5
};

inline constexpr BackgroundImageAlignment kDefaultBackgroundImageAlignment =
    BackgroundImageAlignment::Cover;
inline constexpr int kDefaultBackgroundImageOpacity = 100;

inline constexpr bool isValidBackgroundImageAlignment(int value) noexcept
{
    return value >= int(BackgroundImageAlignment::Top)
        && value <= int(BackgroundImageAlignment::Contain);
}

inline constexpr BackgroundImageAlignment normalizeBackgroundImageAlignment(
    BackgroundImageAlignment alignment) noexcept
{
    return isValidBackgroundImageAlignment(int(alignment))
        ? alignment : kDefaultBackgroundImageAlignment;
}

inline constexpr int backgroundImageAlignmentPersistentValue(
    BackgroundImageAlignment alignment) noexcept
{
    return int(normalizeBackgroundImageAlignment(alignment));
}

inline constexpr BackgroundImageAlignment backgroundImageAlignmentFromPersistentValue(
    int value) noexcept
{
    return normalizeBackgroundImageAlignment(static_cast<BackgroundImageAlignment>(value));
}

} // namespace midi_play::settings

Q_DECLARE_METATYPE(midi_play::settings::BackgroundImageAlignment)
