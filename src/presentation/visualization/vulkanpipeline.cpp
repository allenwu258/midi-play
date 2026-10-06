#include <vulkan/vulkan.h>
#include "vulkanpipeline.h"
#include "vulkanscene.h"
#include <QFile>
#include <array>
#include <cstring>
#include <stdexcept>
#include <vector>

namespace midi_play::presentation::visualization {
namespace {
VkShaderModule shader(VkDevice device, const char* path)
{
    QFile file(QString::fromLatin1(path));
    if (!file.open(QIODevice::ReadOnly)) throw std::runtime_error("Missing embedded Vulkan shader");
    const QByteArray bytes = file.readAll();
    if (bytes.isEmpty() || bytes.size() % 4) throw std::runtime_error("Invalid SPIR-V size");
    std::vector<uint32_t> words(size_t(bytes.size() / 4));
    std::memcpy(words.data(), bytes.constData(), size_t(bytes.size()));
    VkShaderModuleCreateInfo info {VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    info.codeSize = size_t(bytes.size()); info.pCode = words.data();
    VkShaderModule module = VK_NULL_HANDLE;
    if (vkCreateShaderModule(device, &info, nullptr, &module) != VK_SUCCESS)
        throw std::runtime_error("Cannot create Vulkan shader");
    return module;
}
}
VkPipeline createScenePipeline(VkDevice device, VkPipelineLayout layout, VkRenderPass renderPass)
{
    VkShaderModule vertex = VK_NULL_HANDLE, fragment = VK_NULL_HANDLE;
    VkPipeline result = VK_NULL_HANDLE;
    try {
        vertex = shader(device, ":/midi_play/shaders/note.vert.spv");
        fragment = shader(device, ":/midi_play/shaders/note.frag.spv");
        VkPipelineShaderStageCreateInfo stages[2] {};
        for (auto& stage : stages) { stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO; stage.pName = "main"; }
        stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT; stages[0].module = vertex;
        stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT; stages[1].module = fragment;
        VkVertexInputBindingDescription binding {0, sizeof(VulkanQuad), VK_VERTEX_INPUT_RATE_INSTANCE};
        std::array<VkVertexInputAttributeDescription, 7> attributes {};
        for (uint32_t i = 0; i < attributes.size(); ++i) attributes[i] = {i, 0, VK_FORMAT_R32G32B32A32_SFLOAT, i * 16};
        VkPipelineVertexInputStateCreateInfo input {VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
        input.vertexBindingDescriptionCount = 1; input.pVertexBindingDescriptions = &binding;
        input.vertexAttributeDescriptionCount = uint32_t(attributes.size()); input.pVertexAttributeDescriptions = attributes.data();
        VkPipelineInputAssemblyStateCreateInfo assembly {VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
        assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        VkPipelineViewportStateCreateInfo viewport {VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
        viewport.viewportCount = viewport.scissorCount = 1;
        VkPipelineRasterizationStateCreateInfo raster {VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
        raster.polygonMode = VK_POLYGON_MODE_FILL; raster.cullMode = VK_CULL_MODE_NONE; raster.lineWidth = 1;
        VkPipelineMultisampleStateCreateInfo samples {VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
        samples.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        VkPipelineColorBlendAttachmentState blend {};
        blend.blendEnable = VK_TRUE;
        blend.srcColorBlendFactor = blend.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        blend.dstColorBlendFactor = blend.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        blend.colorBlendOp = blend.alphaBlendOp = VK_BLEND_OP_ADD; blend.colorWriteMask = 0xf;
        VkPipelineColorBlendStateCreateInfo blending {VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
        blending.attachmentCount = 1; blending.pAttachments = &blend;
        const VkDynamicState states[] {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
        VkPipelineDynamicStateCreateInfo dynamic {VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
        dynamic.dynamicStateCount = 2; dynamic.pDynamicStates = states;
        VkGraphicsPipelineCreateInfo pipeline {VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
        pipeline.stageCount = 2; pipeline.pStages = stages;
        pipeline.pVertexInputState = &input; pipeline.pInputAssemblyState = &assembly;
        pipeline.pViewportState = &viewport; pipeline.pRasterizationState = &raster;
        pipeline.pMultisampleState = &samples; pipeline.pColorBlendState = &blending;
        pipeline.pDynamicState = &dynamic; pipeline.layout = layout; pipeline.renderPass = renderPass;
        if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipeline, nullptr, &result) != VK_SUCCESS)
            throw std::runtime_error("Cannot create Vulkan scene pipeline");
    } catch (...) {
        if (vertex) vkDestroyShaderModule(device, vertex, nullptr);
        if (fragment) vkDestroyShaderModule(device, fragment, nullptr);
        throw;
    }
    vkDestroyShaderModule(device, vertex, nullptr);
    vkDestroyShaderModule(device, fragment, nullptr);
    return result;
}
} // namespace midi_play::presentation::visualization
