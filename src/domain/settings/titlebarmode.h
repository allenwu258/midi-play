#pragma once
// 标题栏模式及平台可用性，隔离设置文件与 Windows 窗口实现。

#include <QtGlobal>
#include <QMetaType>

namespace midi_play::settings {

// 自定义标题栏仅在 Windows 提供，其他平台始终使用原生标题栏。
enum class TitleBarMode : int {
    Native = 0,
    Custom = 1,
};

#if defined(Q_OS_WIN)
inline constexpr bool kCustomTitleBarAvailable = true;
// Windows 默认使用经过稳定性验证的自定义标题栏；用户仍可显式选择原生标题栏。
inline constexpr TitleBarMode kDefaultTitleBarMode = TitleBarMode::Custom;
#else
inline constexpr bool kCustomTitleBarAvailable = false;
inline constexpr TitleBarMode kDefaultTitleBarMode = TitleBarMode::Native;
#endif

// 返回当前构建平台是否支持自定义标题栏。
constexpr bool isCustomTitleBarAvailable() noexcept
{
    return kCustomTitleBarAvailable;
}

// 检查枚举及平台能力，不能仅凭持久化数值启用自定义模式。
constexpr bool isValidTitleBarMode(TitleBarMode mode) noexcept
{
    return mode == TitleBarMode::Native
        || (mode == TitleBarMode::Custom && kCustomTitleBarAvailable);
}

// 未支持的模式回退原生标题栏。
constexpr TitleBarMode normalizeTitleBarMode(TitleBarMode mode) noexcept
{
    return isValidTitleBarMode(mode) ? mode : TitleBarMode::Native;
}

// 按当前平台规范化后写入整数值。
constexpr int titleBarModePersistentValue(TitleBarMode mode) noexcept
{
    return static_cast<int>(normalizeTitleBarMode(mode));
}

// 只有配置明确选择 Custom 且平台支持时才恢复自定义模式。
constexpr TitleBarMode titleBarModeFromPersistentValue(int value) noexcept
{
    if (value == static_cast<int>(TitleBarMode::Custom) && kCustomTitleBarAvailable) {
        return TitleBarMode::Custom;
    }
    return TitleBarMode::Native;
}

} // namespace midi_play::settings

Q_DECLARE_METATYPE(midi_play::settings::TitleBarMode)
