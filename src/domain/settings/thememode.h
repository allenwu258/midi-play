#pragma once
// 明暗主题的值对象协议及非法配置回退规则。

#include <QMetaType>

namespace midi_play::settings {

// 枚举值与配置文件一致，默认采用深色主题。
enum class ThemeMode : int { Dark = 0, Light = 1 };
inline constexpr ThemeMode kDefaultThemeMode = ThemeMode::Dark;

// 检查原始整数是否属于已支持的主题。
inline constexpr bool isValidThemeMode(int value) noexcept
{
    return value == int(ThemeMode::Dark) || value == int(ThemeMode::Light);
}

// 仅显式 Light 保留浅色，其他输入回退默认深色。
inline constexpr ThemeMode normalizeThemeMode(ThemeMode mode) noexcept
{
    return mode == ThemeMode::Light ? ThemeMode::Light : kDefaultThemeMode;
}

// 序列化前规范化，避免写入未知枚举值。
inline constexpr int themeModePersistentValue(ThemeMode mode) noexcept
{
    return int(normalizeThemeMode(mode));
}

// 将配置整数转换为可用主题。
inline constexpr ThemeMode themeModeFromPersistentValue(int value) noexcept
{
    return normalizeThemeMode(static_cast<ThemeMode>(value));
}

} // namespace midi_play::settings

Q_DECLARE_METATYPE(midi_play::settings::ThemeMode)
