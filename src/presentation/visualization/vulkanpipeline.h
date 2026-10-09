#pragma once
// 实时与离屏 Vulkan 后端共享的 push constant ABI 和图形管线工厂。
#include <vulkan/vulkan.h>

namespace midi_play::presentation::visualization {
// 字段顺序与 GLSL Frame 一致，总长 72 字节，不得按 CPU 习惯重新排列。
struct FrameConstants {
    // 输出物理尺寸、相对基准的播放秒数、逻辑像素击键线位置。
    float width, height, positionSeconds, strikeY;
    float pixelsPerSecond, clipTop, clipBottom, dpr; // 时间比例及逻辑裁剪边界。
    float bodyOpacity, noteHaloStrength, noteEdgeStrength, noteSheenStrength;
    float strikeGlowStrength, keyGlowStrength, particleStrength;
    float offsetX = 0, offsetY = 0; // 离屏输出居中偏移，单位为物理像素。
    float animationSeconds = 0; // 独立于相对时间基准的视觉动画时间。
};
static_assert(sizeof(FrameConstants) == 72);
// 创建单采样、预乘 alpha 混合的实例管线；调用方负责设备有效性与管线销毁。
// 创建失败抛出异常，并释放已创建的临时着色器模块。
VkPipeline createScenePipeline(VkDevice device, VkPipelineLayout layout, VkRenderPass renderPass);
} // namespace midi_play::presentation::visualization
