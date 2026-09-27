#pragma once

#include <QMetaType>

namespace midi_play::settings {

enum class VisualEffectLevel : int { Low = 0, Medium = 1, High = 2 };
inline constexpr VisualEffectLevel kDefaultVisualEffectLevel = VisualEffectLevel::Medium;

inline constexpr bool isValidVisualEffectLevel(int value) noexcept
{
    return value >= int(VisualEffectLevel::Low) && value <= int(VisualEffectLevel::High);
}

inline constexpr VisualEffectLevel normalizeVisualEffectLevel(VisualEffectLevel level) noexcept
{
    return isValidVisualEffectLevel(int(level)) ? level : kDefaultVisualEffectLevel;
}

inline constexpr int visualEffectLevelPersistentValue(VisualEffectLevel level) noexcept
{
    return int(normalizeVisualEffectLevel(level));
}

inline constexpr VisualEffectLevel visualEffectLevelFromPersistentValue(int value) noexcept
{
    return normalizeVisualEffectLevel(static_cast<VisualEffectLevel>(value));
}

} // namespace midi_play::settings

Q_DECLARE_METATYPE(midi_play::settings::VisualEffectLevel)
