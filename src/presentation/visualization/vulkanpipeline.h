#pragma once
#include <vulkan/vulkan.h>

namespace midi_play::presentation::visualization {
struct FrameConstants {
    float width, height, positionSeconds, strikeY;
    float pixelsPerSecond, clipTop, clipBottom, dpr;
    float bodyOpacity, noteHaloStrength, noteEdgeStrength, noteSheenStrength;
    float strikeGlowStrength, keyGlowStrength, particleStrength;
    float offsetX = 0, offsetY = 0;
    float animationSeconds = 0;
};
static_assert(sizeof(FrameConstants) == 72);
VkPipeline createScenePipeline(VkDevice device, VkPipelineLayout layout, VkRenderPass renderPass);
} // namespace midi_play::presentation::visualization
