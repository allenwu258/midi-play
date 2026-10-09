#pragma once
// 视觉特效等级的配置协议，实际强度由呈现后端解释。

#include <QMetaType>

namespace midi_play::settings {

// 连续整数范围便于配置校验，默认中档。
enum class VisualEffectLevel : int { Low = 0, Medium = 1, High = 2 };
inline constexpr VisualEffectLevel kDefaultVisualEffectLevel = VisualEffectLevel::Medium;

// 检查持久化值是否位于支持的等级范围。
inline constexpr bool isValidVisualEffectLevel(int value) noexcept
{
    return value >= int(VisualEffectLevel::Low) && value <= int(VisualEffectLevel::High);
}

// 非法等级回退中档，保持已支持值不变。
inline constexpr VisualEffectLevel normalizeVisualEffectLevel(VisualEffectLevel level) noexcept
{
    return isValidVisualEffectLevel(int(level)) ? level : kDefaultVisualEffectLevel;
}

// 写入规范化的等级整数。
inline constexpr int visualEffectLevelPersistentValue(VisualEffectLevel level) noexcept
{
    return int(normalizeVisualEffectLevel(level));
}

// 从配置整数恢复有效等级。
inline constexpr VisualEffectLevel visualEffectLevelFromPersistentValue(int value) noexcept
{
    return normalizeVisualEffectLevel(static_cast<VisualEffectLevel>(value));
}

} // namespace midi_play::settings

Q_DECLARE_METATYPE(midi_play::settings::VisualEffectLevel)
