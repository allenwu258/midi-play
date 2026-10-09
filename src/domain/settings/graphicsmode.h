#pragma once
// 图形后端枚举与稳定的持久化映射，供设置、GUI 和渲染宿主共用。

#include <QMetaType>

namespace midi_play::settings {

// 数值是配置文件协议的一部分，新增模式时须保留已有映射。
enum class GraphicsMode : int {
    Traditional = 0,
    VulkanExperimental = 1
};

// 新安装优先选择 Vulkan；已有持久化值优先，保留用户选择的后端。
inline constexpr GraphicsMode kDefaultGraphicsMode = GraphicsMode::VulkanExperimental;

// 非 Vulkan 值统一序列化为传统后端。
inline constexpr int graphicsModePersistentValue(GraphicsMode mode) noexcept
{
    return mode == GraphicsMode::VulkanExperimental ? 1 : 0;
}

// 仅配置值 1 表示 Vulkan，其他值回退传统绘制。
inline constexpr GraphicsMode graphicsModeFromPersistentValue(int value) noexcept
{
    return value == 1 ? GraphicsMode::VulkanExperimental : GraphicsMode::Traditional;
}

// 规范化外部枚举输入，非法值采用传统后端。
inline constexpr GraphicsMode normalizeGraphicsMode(GraphicsMode mode) noexcept
{
    return mode == GraphicsMode::VulkanExperimental
        ? GraphicsMode::VulkanExperimental : GraphicsMode::Traditional;
}

} // namespace midi_play::settings

Q_DECLARE_METATYPE(midi_play::settings::GraphicsMode)
