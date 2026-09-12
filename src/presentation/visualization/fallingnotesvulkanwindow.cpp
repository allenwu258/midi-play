#include "fallingnotesvulkanwindow.h"
#include "vulkanscene.h"

#include <QFile>
#include <QGuiApplication>
#include <QLoggingCategory>
#include <QVulkanFunctions>
#include <algorithm>
#include <array>
#include <cstring>
#include <stdexcept>

namespace midi_play::presentation::visualization {
namespace {
Q_LOGGING_CATEGORY(lcVulkan, "midi_play.vulkan")

void checked(VkResult result, const char* operation)
{
    if (result != VK_SUCCESS)
        throw std::runtime_error(QStringLiteral("%1: VkResult %2").arg(QString::fromLatin1(operation)).arg(result).toStdString());
}

struct Buffer {
    VkBuffer handle = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    void* mapped = nullptr;
    VkDeviceSize capacity = 0;
    bool coherent = false;
};

struct FrameResources {
    Buffer notes;
    Buffer ui;
    Buffer staging;
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory imageMemory = VK_NULL_HANDLE;
    VkImageView imageView = VK_NULL_HANDLE;
    VkDescriptorSet descriptor = VK_NULL_HANDLE;
    quint64 notesRevision = 0;
    quint64 atlasRevision = 0;
};

struct FrameConstants {
    float width;
    float height;
    float positionSeconds;
    float strikeY;
    float pixelsPerSecond;
    float clipTop;
    float clipBottom;
    float dpr;
};
static_assert(sizeof(FrameConstants) == 32);
}

class FallingNotesVulkanRenderer final : public QVulkanWindowRenderer {
public:
    explicit FallingNotesVulkanRenderer(FallingNotesVulkanWindow* window) : m_window(window) {}
    void initResources() override;
    void initSwapChainResources() override;
    void releaseSwapChainResources() override;
    void releaseResources() override;
    void startNextFrame() override;
    void logicalDeviceLost() override { fail(QStringLiteral("Vulkan 图形设备已丢失")); }
    void physicalDeviceLost() override { fail(QStringLiteral("Vulkan 图形设备不可用")); }

private:
    uint32_t memoryType(uint32_t bits, VkMemoryPropertyFlags required, VkMemoryPropertyFlags preferred = 0);
    void destroy(Buffer& buffer);
    void reserve(Buffer& buffer, VkDeviceSize bytes, VkBufferUsageFlags usage);
    void upload(Buffer& buffer, const void* source, VkDeviceSize bytes);
    VkShaderModule shader(const char* path);
    void createPipeline();
    void createAtlas(FrameResources& frame);
    void uploadAtlas(FrameResources& frame, VkCommandBuffer command);
    void fail(const QString& message);
    void draw(VkCommandBuffer command, Buffer& buffer, uint32_t count, uint32_t first = 0);

    FallingNotesVulkanWindow* m_window;
    QVulkanDeviceFunctions* m_df = nullptr;
    VkDevice m_device = VK_NULL_HANDLE;
    VkPhysicalDeviceMemoryProperties m_memoryProperties {};
    VkDescriptorSetLayout m_descriptorLayout = VK_NULL_HANDLE;
    VkDescriptorPool m_descriptorPool = VK_NULL_HANDLE;
    VkSampler m_sampler = VK_NULL_HANDLE;
    VkPipelineLayout m_layout = VK_NULL_HANDLE;
    VkPipeline m_pipeline = VK_NULL_HANDLE;
    std::array<FrameResources, QVulkanWindow::MAX_CONCURRENT_FRAME_COUNT> m_frames {};
    VulkanScene m_scene;
    QVector<VulkanQuad> m_ui;
    bool m_failed = false;
};

uint32_t FallingNotesVulkanRenderer::memoryType(uint32_t bits, VkMemoryPropertyFlags required,
                                               VkMemoryPropertyFlags preferred)
{
    uint32_t fallback = UINT32_MAX;
    for (uint32_t i = 0; i < m_memoryProperties.memoryTypeCount; ++i) {
        const auto flags = m_memoryProperties.memoryTypes[i].propertyFlags;
        if (!(bits & (1u << i)) || (flags & required) != required) continue;
        if ((flags & preferred) == preferred) return i;
        fallback = i;
    }
    if (fallback == UINT32_MAX) throw std::runtime_error("No suitable Vulkan memory type");
    return fallback;
}

void FallingNotesVulkanRenderer::destroy(Buffer& buffer)
{
    if (buffer.mapped) m_df->vkUnmapMemory(m_device, buffer.memory);
    if (buffer.handle) m_df->vkDestroyBuffer(m_device, buffer.handle, nullptr);
    if (buffer.memory) m_df->vkFreeMemory(m_device, buffer.memory, nullptr);
    buffer = {};
}

void FallingNotesVulkanRenderer::reserve(Buffer& buffer, VkDeviceSize bytes, VkBufferUsageFlags usage)
{
    if (buffer.capacity >= bytes && buffer.handle) return;
    // Only the current, fence-completed Qt frame slot is modified here.
    destroy(buffer);
    VkBufferCreateInfo info {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    info.size = std::max<VkDeviceSize>(4096, bytes + bytes / 2);
    info.usage = usage;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    checked(m_df->vkCreateBuffer(m_device, &info, nullptr, &buffer.handle), "create buffer");
    VkMemoryRequirements requirements;
    m_df->vkGetBufferMemoryRequirements(m_device, buffer.handle, &requirements);
    const auto type = memoryType(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
                                  VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    VkMemoryAllocateInfo allocation {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    allocation.allocationSize = requirements.size;
    allocation.memoryTypeIndex = type;
    checked(m_df->vkAllocateMemory(m_device, &allocation, nullptr, &buffer.memory), "allocate buffer");
    checked(m_df->vkBindBufferMemory(m_device, buffer.handle, buffer.memory, 0), "bind buffer");
    checked(m_df->vkMapMemory(m_device, buffer.memory, 0, VK_WHOLE_SIZE, 0, &buffer.mapped), "map buffer");
    buffer.capacity = info.size;
    buffer.coherent = (m_memoryProperties.memoryTypes[type].propertyFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) != 0;
}

void FallingNotesVulkanRenderer::upload(Buffer& buffer, const void* source, VkDeviceSize bytes)
{
    if (!bytes) return;
    std::memcpy(buffer.mapped, source, static_cast<size_t>(bytes));
    if (!buffer.coherent) {
        VkMappedMemoryRange range {VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE};
        range.memory = buffer.memory;
        range.size = VK_WHOLE_SIZE;
        checked(m_df->vkFlushMappedMemoryRanges(m_device, 1, &range), "flush buffer");
    }
}

void FallingNotesVulkanRenderer::fail(const QString& message)
{
    if (m_failed) return;
    m_failed = true;
    qCWarning(lcVulkan) << message;
    emit m_window->initializationFailed(message);
}

void FallingNotesVulkanRenderer::initResources()
{
    m_failed = false;
    m_device = m_window->device();
    m_df = m_window->vulkanInstance()->deviceFunctions(m_device);
    m_window->vulkanInstance()->functions()->vkGetPhysicalDeviceMemoryProperties(m_window->physicalDevice(), &m_memoryProperties);
    try {
        VkDescriptorSetLayoutBinding binding {};
        binding.binding = 0;
        binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        binding.descriptorCount = 1;
        binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        VkDescriptorSetLayoutCreateInfo descriptor {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        descriptor.bindingCount = 1;
        descriptor.pBindings = &binding;
        checked(m_df->vkCreateDescriptorSetLayout(m_device, &descriptor, nullptr, &m_descriptorLayout), "descriptor layout");
        const uint32_t count = uint32_t(m_window->concurrentFrameCount());
        VkDescriptorPoolSize poolSize {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, count};
        VkDescriptorPoolCreateInfo pool {VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        pool.maxSets = count;
        pool.poolSizeCount = 1;
        pool.pPoolSizes = &poolSize;
        checked(m_df->vkCreateDescriptorPool(m_device, &pool, nullptr, &m_descriptorPool), "descriptor pool");
        for (uint32_t i = 0; i < count; ++i) {
            VkDescriptorSetAllocateInfo allocation {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
            allocation.descriptorPool = m_descriptorPool;
            allocation.descriptorSetCount = 1;
            allocation.pSetLayouts = &m_descriptorLayout;
            checked(m_df->vkAllocateDescriptorSets(m_device, &allocation, &m_frames[i].descriptor), "descriptor set");
        }
        VkSamplerCreateInfo sampler {VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
        sampler.magFilter = sampler.minFilter = VK_FILTER_LINEAR;
        sampler.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        sampler.addressModeU = sampler.addressModeV = sampler.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        checked(m_df->vkCreateSampler(m_device, &sampler, nullptr, &m_sampler), "atlas sampler");
        VkPushConstantRange constants {VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(FrameConstants)};
        VkPipelineLayoutCreateInfo layout {VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        layout.setLayoutCount = 1;
        layout.pSetLayouts = &m_descriptorLayout;
        layout.pushConstantRangeCount = 1;
        layout.pPushConstantRanges = &constants;
        checked(m_df->vkCreatePipelineLayout(m_device, &layout, nullptr, &m_layout), "pipeline layout");
        qCInfo(lcVulkan) << "GPU:" << m_window->physicalDeviceProperties()->deviceName
                        << "frame slots:" << count;
    } catch (const std::exception& error) { fail(QString::fromUtf8(error.what())); }
}

VkShaderModule FallingNotesVulkanRenderer::shader(const char* path)
{
    QFile file(QString::fromLatin1(path));
    if (!file.open(QIODevice::ReadOnly)) throw std::runtime_error("Missing embedded Vulkan shader");
    const QByteArray bytes = file.readAll();
    if (bytes.isEmpty() || bytes.size() % 4) throw std::runtime_error("Invalid SPIR-V size");
    std::vector<uint32_t> words(size_t(bytes.size() / 4));
    std::memcpy(words.data(), bytes.constData(), size_t(bytes.size()));
    VkShaderModuleCreateInfo info {VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    info.codeSize = size_t(bytes.size());
    info.pCode = words.data();
    VkShaderModule result = VK_NULL_HANDLE;
    checked(m_df->vkCreateShaderModule(m_device, &info, nullptr, &result), "shader module");
    return result;
}

void FallingNotesVulkanRenderer::createPipeline()
{
    VkShaderModule vertex = VK_NULL_HANDLE;
    VkShaderModule fragment = VK_NULL_HANDLE;
    try {
        vertex = shader(":/midi_play/shaders/note.vert.spv");
        fragment = shader(":/midi_play/shaders/note.frag.spv");
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
        samples.rasterizationSamples = m_window->sampleCountFlagBits();
        VkPipelineDepthStencilStateCreateInfo depth {VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
        VkPipelineColorBlendAttachmentState blend {};
        blend.blendEnable = VK_TRUE;
        blend.srcColorBlendFactor = blend.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        blend.dstColorBlendFactor = blend.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        blend.colorBlendOp = blend.alphaBlendOp = VK_BLEND_OP_ADD;
        blend.colorWriteMask = 0xf;
        VkPipelineColorBlendStateCreateInfo blending {VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
        blending.attachmentCount = 1; blending.pAttachments = &blend;
        const VkDynamicState states[] {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
        VkPipelineDynamicStateCreateInfo dynamic {VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
        dynamic.dynamicStateCount = 2; dynamic.pDynamicStates = states;
        VkGraphicsPipelineCreateInfo pipeline {VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
        pipeline.stageCount = 2; pipeline.pStages = stages;
        pipeline.pVertexInputState = &input; pipeline.pInputAssemblyState = &assembly;
        pipeline.pViewportState = &viewport; pipeline.pRasterizationState = &raster;
        pipeline.pMultisampleState = &samples; pipeline.pDepthStencilState = &depth;
        pipeline.pColorBlendState = &blending; pipeline.pDynamicState = &dynamic;
        pipeline.layout = m_layout; pipeline.renderPass = m_window->defaultRenderPass();
        checked(m_df->vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &pipeline, nullptr, &m_pipeline), "graphics pipeline");
    } catch (...) {
        if (vertex) m_df->vkDestroyShaderModule(m_device, vertex, nullptr);
        if (fragment) m_df->vkDestroyShaderModule(m_device, fragment, nullptr);
        throw;
    }
    m_df->vkDestroyShaderModule(m_device, vertex, nullptr);
    m_df->vkDestroyShaderModule(m_device, fragment, nullptr);
}

void FallingNotesVulkanRenderer::initSwapChainResources()
{
    if (m_failed) return;
    try { createPipeline(); }
    catch (const std::exception& error) { fail(QString::fromUtf8(error.what())); }
}

void FallingNotesVulkanRenderer::releaseSwapChainResources()
{
    if (m_pipeline) m_df->vkDestroyPipeline(m_device, m_pipeline, nullptr);
    m_pipeline = VK_NULL_HANDLE;
}

void FallingNotesVulkanRenderer::releaseResources()
{
    if (!m_device) return;
    // Qt waits for its device work before entering resource release callbacks.
    releaseSwapChainResources();
    for (auto& frame : m_frames) {
        destroy(frame.notes); destroy(frame.ui); destroy(frame.staging);
        if (frame.imageView) m_df->vkDestroyImageView(m_device, frame.imageView, nullptr);
        if (frame.image) m_df->vkDestroyImage(m_device, frame.image, nullptr);
        if (frame.imageMemory) m_df->vkFreeMemory(m_device, frame.imageMemory, nullptr);
        frame = {};
    }
    if (m_sampler) m_df->vkDestroySampler(m_device, m_sampler, nullptr);
    if (m_layout) m_df->vkDestroyPipelineLayout(m_device, m_layout, nullptr);
    if (m_descriptorPool) m_df->vkDestroyDescriptorPool(m_device, m_descriptorPool, nullptr);
    if (m_descriptorLayout) m_df->vkDestroyDescriptorSetLayout(m_device, m_descriptorLayout, nullptr);
    m_sampler = VK_NULL_HANDLE; m_layout = VK_NULL_HANDLE;
    m_descriptorPool = VK_NULL_HANDLE; m_descriptorLayout = VK_NULL_HANDLE;
    m_device = VK_NULL_HANDLE; m_df = nullptr;
}

void FallingNotesVulkanRenderer::createAtlas(FrameResources& frame)
{
    VkImageCreateInfo info {VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    info.imageType = VK_IMAGE_TYPE_2D; info.format = VK_FORMAT_R8G8B8A8_UNORM;
    info.extent = {2048, 2048, 1}; info.mipLevels = info.arrayLayers = 1;
    info.samples = VK_SAMPLE_COUNT_1_BIT; info.tiling = VK_IMAGE_TILING_OPTIMAL;
    info.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    checked(m_df->vkCreateImage(m_device, &info, nullptr, &frame.image), "atlas image");
    VkMemoryRequirements requirements;
    m_df->vkGetImageMemoryRequirements(m_device, frame.image, &requirements);
    VkMemoryAllocateInfo allocation {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    allocation.allocationSize = requirements.size;
    allocation.memoryTypeIndex = memoryType(requirements.memoryTypeBits, 0, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    checked(m_df->vkAllocateMemory(m_device, &allocation, nullptr, &frame.imageMemory), "atlas memory");
    checked(m_df->vkBindImageMemory(m_device, frame.image, frame.imageMemory, 0), "bind atlas");
    VkImageViewCreateInfo view {VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    view.image = frame.image; view.viewType = VK_IMAGE_VIEW_TYPE_2D; view.format = info.format;
    view.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    checked(m_df->vkCreateImageView(m_device, &view, nullptr, &frame.imageView), "atlas view");
    VkDescriptorImageInfo imageInfo {m_sampler, frame.imageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkWriteDescriptorSet write {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    write.dstSet = frame.descriptor; write.dstBinding = 0;
    write.descriptorCount = 1; write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.pImageInfo = &imageInfo;
    m_df->vkUpdateDescriptorSets(m_device, 1, &write, 0, nullptr);
}

void FallingNotesVulkanRenderer::uploadAtlas(FrameResources& frame, VkCommandBuffer command)
{
    if (frame.atlasRevision == m_scene.atlasRevision()) return;
    if (!frame.image) createAtlas(frame);
    const auto& image = m_scene.atlas();
    reserve(frame.staging, image.sizeInBytes(), VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
    upload(frame.staging, image.constBits(), image.sizeInBytes());
    VkImageMemoryBarrier barrier {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    barrier.srcAccessMask = frame.atlasRevision ? VK_ACCESS_SHADER_READ_BIT : 0;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.oldLayout = frame.atlasRevision ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = frame.image;
    barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    m_df->vkCmdPipelineBarrier(command, frame.atlasRevision ? VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
    VkBufferImageCopy copy {};
    copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    copy.imageExtent = {2048, 2048, 1};
    m_df->vkCmdCopyBufferToImage(command, frame.staging.handle, frame.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL; barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    m_df->vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        0, 0, nullptr, 0, nullptr, 1, &barrier);
    frame.atlasRevision = m_scene.atlasRevision();
}

void FallingNotesVulkanRenderer::draw(VkCommandBuffer command, Buffer& buffer, uint32_t count, uint32_t first)
{
    if (!count) return;
    const VkDeviceSize offset = 0;
    m_df->vkCmdBindVertexBuffers(command, 0, 1, &buffer.handle, &offset);
    m_df->vkCmdDraw(command, 6, count, 0, first);
}

void FallingNotesVulkanRenderer::startNextFrame()
{
    VkCommandBuffer command = m_window->currentCommandBuffer();
    auto& frame = m_frames[size_t(m_window->currentFrame())];
    // A failed or lost device must not receive further render-pass commands.
    // QVulkanWindow still requires frameReady() to release the frame slot.
    if (m_failed || !m_device || !m_df) {
        m_window->frameReady();
        m_window->frameCompleted(false);
        return;
    }
    if (!m_failed) {
        try {
            const auto state = m_window->sceneState();
            m_scene.prepare(state, m_window->size(), m_window->devicePixelRatio(), m_window->sceneFont());
            if (frame.notesRevision != m_scene.notesRevision()) {
                reserve(frame.notes, VkDeviceSize(m_scene.notes().size()) * sizeof(VulkanQuad), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
                upload(frame.notes, m_scene.notes().constData(), VkDeviceSize(m_scene.notes().size()) * sizeof(VulkanQuad));
                frame.notesRevision = m_scene.notesRevision();
            }
            m_ui = m_scene.background();
            m_ui.append(m_scene.foreground());
            reserve(frame.ui, VkDeviceSize(m_ui.size()) * sizeof(VulkanQuad), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
            upload(frame.ui, m_ui.constData(), VkDeviceSize(m_ui.size()) * sizeof(VulkanQuad));
            uploadAtlas(frame, command);
        } catch (const std::exception& error) { fail(QString::fromUtf8(error.what())); }
    }
    VkClearValue clear[2] {};
    clear[0].color = {{18.f/255, 20.f/255, 22.f/255, 1}};
    clear[1].depthStencil = {1, 0};
    const QSize physical = m_window->swapChainImageSize();
    VkRenderPassBeginInfo begin {VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    begin.renderPass = m_window->defaultRenderPass(); begin.framebuffer = m_window->currentFramebuffer();
    begin.renderArea.extent = {uint32_t(physical.width()), uint32_t(physical.height())};
    begin.clearValueCount = 2; begin.pClearValues = clear;
    m_df->vkCmdBeginRenderPass(command, &begin, VK_SUBPASS_CONTENTS_INLINE);
    if (!m_failed && m_pipeline) {
        const auto& g = m_scene.geometry();
        const auto state = m_window->sceneState();
        FrameConstants constants {float(m_window->width()), float(m_window->height()),
            float((state.transportPositionUs - m_scene.timeOriginUs())/1'000'000.0), float(g.strikeLineY),
            float(g.pixelsPerMicrosecond*1'000'000), float(g.fallingRect.top()), float(g.fallingRect.bottom()), float(m_window->devicePixelRatio())};
        VkViewport viewport {0, 0, float(physical.width()), float(physical.height()), 0, 1};
        VkRect2D scissor {{0, 0}, begin.renderArea.extent};
        m_df->vkCmdSetViewport(command, 0, 1, &viewport);
        m_df->vkCmdSetScissor(command, 0, 1, &scissor);
        m_df->vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);
        m_df->vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_GRAPHICS, m_layout, 0, 1, &frame.descriptor, 0, nullptr);
        m_df->vkCmdPushConstants(command, m_layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(constants), &constants);
        draw(command, frame.ui, uint32_t(m_scene.background().size()));
        draw(command, frame.notes, uint32_t(m_scene.notes().size()));
        draw(command, frame.ui, uint32_t(m_scene.foreground().size()), uint32_t(m_scene.background().size()));
    }
    m_df->vkCmdEndRenderPass(command);
    m_window->frameReady();
    m_window->frameCompleted(!m_failed);
}

FallingNotesVulkanWindow::FallingNotesVulkanWindow(QWindow* parent) : QVulkanWindow(parent)
{
    setFlags(PersistentResources);
    setPreferredColorFormats({VK_FORMAT_B8G8R8A8_UNORM, VK_FORMAT_R8G8B8A8_UNORM});
    setSampleCount(1);
}

void FallingNotesVulkanWindow::setChart(midi_play::visualization::VisualChartPtr chart)
{
    m_chart = std::move(chart);
    requestUpdate();
}

void FallingNotesVulkanWindow::setTransportPosition(qint64 position, qint64 duration)
{
    m_positionUs = std::clamp(position, qint64(0), std::max<qint64>(0, duration));
    m_durationUs = std::max<qint64>(0, duration);
    requestUpdate();
}

void FallingNotesVulkanWindow::setTransportState(midi_play::playback::State state)
{
    m_state = state;
    requestUpdate();
}

midi_play::visualization::PlaybackSceneState FallingNotesVulkanWindow::sceneState() const
{
    midi_play::visualization::PlaybackSceneState result;
    result.chart = m_chart;
    result.transportPositionUs = m_positionUs;
    result.durationUs = m_durationUs;
    result.transportState = m_state;
    result.loading = m_loading;
    result.errorMessage = m_error;
    result.updateVisibleWindow();
    return result;
}

void FallingNotesVulkanWindow::frameCompleted(bool success)
{
    if (success) emit frameRendered();
    if (success && m_state == midi_play::playback::State::Playing && isExposed()) requestUpdate();
}

QVulkanWindowRenderer* FallingNotesVulkanWindow::createRenderer()
{
    return new FallingNotesVulkanRenderer(this);
}

} // namespace midi_play::presentation::visualization
