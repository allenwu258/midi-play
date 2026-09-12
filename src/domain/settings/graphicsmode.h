#pragma once

#include <QMetaType>

namespace midi_play::settings {

enum class GraphicsMode : int {
    Traditional = 0,
    VulkanExperimental = 1
};

inline constexpr GraphicsMode kDefaultGraphicsMode = GraphicsMode::Traditional;

inline constexpr int graphicsModePersistentValue(GraphicsMode mode) noexcept
{
    return mode == GraphicsMode::VulkanExperimental ? 1 : 0;
}

inline constexpr GraphicsMode graphicsModeFromPersistentValue(int value) noexcept
{
    return value == 1 ? GraphicsMode::VulkanExperimental : GraphicsMode::Traditional;
}

inline constexpr GraphicsMode normalizeGraphicsMode(GraphicsMode mode) noexcept
{
    return mode == GraphicsMode::VulkanExperimental
        ? GraphicsMode::VulkanExperimental : GraphicsMode::Traditional;
}

} // namespace midi_play::settings

Q_DECLARE_METATYPE(midi_play::settings::GraphicsMode)
