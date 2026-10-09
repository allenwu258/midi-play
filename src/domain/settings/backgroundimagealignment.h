#pragma once
// 背景适配模式及默认透明度，为两种绘制后端提供统一配置语义。

#include <QMetaType>

namespace midi_play::settings {

// 传统与 Vulkan 后端共用放置模式，持久化偏好在两条路径中含义一致。
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

// 检查原始配置整数是否属于连续的适配模式范围。
inline constexpr bool isValidBackgroundImageAlignment(int value) noexcept
{
    return value >= int(BackgroundImageAlignment::Top)
        && value <= int(BackgroundImageAlignment::Contain);
}

// 未知模式回退 Cover，即等比铺满并裁剪。
inline constexpr BackgroundImageAlignment normalizeBackgroundImageAlignment(
    BackgroundImageAlignment alignment) noexcept
{
    return isValidBackgroundImageAlignment(int(alignment))
        ? alignment : kDefaultBackgroundImageAlignment;
}

// 序列化前按默认规则规范化。
inline constexpr int backgroundImageAlignmentPersistentValue(
    BackgroundImageAlignment alignment) noexcept
{
    return int(normalizeBackgroundImageAlignment(alignment));
}

// 从持久化整数恢复适配模式。
inline constexpr BackgroundImageAlignment backgroundImageAlignmentFromPersistentValue(
    int value) noexcept
{
    return normalizeBackgroundImageAlignment(static_cast<BackgroundImageAlignment>(value));
}

} // namespace midi_play::settings

Q_DECLARE_METATYPE(midi_play::settings::BackgroundImageAlignment)
