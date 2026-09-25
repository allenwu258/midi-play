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
#include <vector>

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

struct ImageResources {
    VkFramebuffer framebuffer = VK_NULL_HANDLE;
    Buffer notes;
    Buffer staticUi;
    Buffer dynamicUi;
    QVector<VulkanQuad> uploadedDynamicUi;
    Buffer staging;
    Buffer backgroundStaging;
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory imageMemory = VK_NULL_HANDLE;
    VkImageView imageView = VK_NULL_HANDLE;
    VkImage backgroundImage = VK_NULL_HANDLE;
    VkDeviceMemory backgroundImageMemory = VK_NULL_HANDLE;
    VkImageView backgroundImageView = VK_NULL_HANDLE;
    VkDescriptorSet descriptor = VK_NULL_HANDLE;
    quint64 notesRevision = 0;
    quint64 staticUiRevision = 0;
    quint64 atlasRevision = 0;
    quint64 backgroundRevision = 0;
    // Optional host/GPU lifetime audit, enabled by the Vulkan stress test.
    VkEvent completionEvent = VK_NULL_HANDLE;
    bool submitted = false;
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
    float bodyOpacity;
};
static_assert(sizeof(FrameConstants) == 36);
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
    void upload(Buffer& buffer, const void* source, VkDeviceSize bytes, VkDeviceSize offset = 0);
    void uploadDynamicUi(ImageResources& frame);
    VkShaderModule shader(const char* path);
    void createPipeline();
    void createRenderPass();
    void createAtlas(ImageResources& frame);
    void uploadAtlas(ImageResources& frame, VkCommandBuffer command);
    void uploadBackground(ImageResources& frame, VkCommandBuffer command);
    void completePresentation();
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
    VkRenderPass m_renderPass = VK_NULL_HANDLE;
    std::vector<ImageResources> m_images;
    VulkanScene m_scene;
    bool m_failed = false;
    bool m_auditResources = false;
    bool m_separatePresentQueue = false;
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
    // Only resources owned by the acquired, fence-completed swapchain image
    // may be updated or replaced. currentFrame() is a different rotation.
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
    qCDebug(lcVulkan) << "Buffer capacity:" << quint64(buffer.capacity);
}

void FallingNotesVulkanRenderer::upload(Buffer& buffer, const void* source, VkDeviceSize bytes, VkDeviceSize offset)
{
    if (!bytes) return;
    if (offset > buffer.capacity || bytes > buffer.capacity - offset)
        throw std::runtime_error("Vulkan upload exceeds buffer capacity");
    std::memcpy(static_cast<char*>(buffer.mapped) + offset, source, static_cast<size_t>(bytes));
    if (!buffer.coherent) {
        VkMappedMemoryRange range {VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE};
        range.memory = buffer.memory;
        range.size = VK_WHOLE_SIZE;
        checked(m_df->vkFlushMappedMemoryRanges(m_device, 1, &range), "flush buffer");
    }
}

void FallingNotesVulkanRenderer::uploadDynamicUi(ImageResources& frame)
{
    const auto& quads = m_scene.dynamicUi().quads;
    const auto bytes = VkDeviceSize(quads.size()) * sizeof(VulkanQuad);
    reserve(frame.dynamicUi, bytes, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
    auto& previous = frame.uploadedDynamicUi;
    if (previous.size() != quads.size()) {
        upload(frame.dynamicUi, quads.constData(), bytes);
    } else {
        // Preserve unchanged labels and inactive effects in this image's copy.
        // Host-coherent memory still requires the image's completion fence.
        for (qsizetype first = 0; first < quads.size();) {
            if (previous[first] == quads[first]) { ++first; continue; }
            qsizetype end = first + 1;
            while (end < quads.size() && previous[end] != quads[end]) ++end;
            upload(frame.dynamicUi, quads.constData() + first,
                   VkDeviceSize(end - first) * sizeof(VulkanQuad), VkDeviceSize(first) * sizeof(VulkanQuad));
            first = end;
        }
    }
    previous = quads;
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
    m_auditResources = qEnvironmentVariableIntValue("MIDI_PLAY_VULKAN_VALIDATE_RESOURCES") != 0;
    m_device = m_window->device();
    m_df = m_window->vulkanInstance()->deviceFunctions(m_device);
    m_separatePresentQueue = !m_window->vulkanInstance()->supportsPresent(
        m_window->physicalDevice(), m_window->graphicsQueueFamilyIndex(), m_window);
    m_window->vulkanInstance()->functions()->vkGetPhysicalDeviceMemoryProperties(m_window->physicalDevice(), &m_memoryProperties);
    try {
        VkDescriptorSetLayoutBinding bindings[2] {};
        for (uint32_t i = 0; i < 2; ++i) {
            bindings[i].binding = i;
            bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            bindings[i].descriptorCount = 1;
            bindings[i].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        }
        VkDescriptorSetLayoutCreateInfo descriptor {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        descriptor.bindingCount = 2;
        descriptor.pBindings = bindings;
        checked(m_df->vkCreateDescriptorSetLayout(m_device, &descriptor, nullptr, &m_descriptorLayout), "descriptor layout");
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
                        << "frame slots:" << m_window->concurrentFrameCount();
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
        pipeline.layout = m_layout; pipeline.renderPass = m_renderPass;
        checked(m_df->vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &pipeline, nullptr, &m_pipeline), "graphics pipeline");
    } catch (...) {
        if (vertex) m_df->vkDestroyShaderModule(m_device, vertex, nullptr);
        if (fragment) m_df->vkDestroyShaderModule(m_device, fragment, nullptr);
        throw;
    }
    m_df->vkDestroyShaderModule(m_device, vertex, nullptr);
    m_df->vkDestroyShaderModule(m_device, fragment, nullptr);
}

void FallingNotesVulkanRenderer::createRenderPass()
{
    // This 2D renderer has no depth attachment. Explicitly chain the initial
    // layout transition to Qt's COLOR_ATTACHMENT_OUTPUT acquire-semaphore wait.
    VkAttachmentDescription color {};
    color.format = m_window->colorFormat();
    color.samples = VK_SAMPLE_COUNT_1_BIT;
    color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    color.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    color.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    color.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    VkAttachmentReference colorReference {0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription subpass {};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorReference;
    VkSubpassDependency dependency {};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    VkRenderPassCreateInfo info {VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    info.attachmentCount = 1;
    info.pAttachments = &color;
    info.subpassCount = 1;
    info.pSubpasses = &subpass;
    info.dependencyCount = 1;
    info.pDependencies = &dependency;
    checked(m_df->vkCreateRenderPass(m_device, &info, nullptr, &m_renderPass), "render pass");
}

void FallingNotesVulkanRenderer::initSwapChainResources()
{
    if (m_failed) return;
    try {
        const uint32_t count = uint32_t(m_window->swapChainImageCount());
        m_images.resize(count);
        createRenderPass();
        VkDescriptorPoolSize poolSize {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, count * 2};
        VkDescriptorPoolCreateInfo pool {VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        pool.maxSets = count;
        pool.poolSizeCount = 1;
        pool.pPoolSizes = &poolSize;
        checked(m_df->vkCreateDescriptorPool(m_device, &pool, nullptr, &m_descriptorPool), "descriptor pool");
        for (uint32_t i = 0; i < count; ++i) {
            auto& frame = m_images[i];
            const auto view = m_window->swapChainImageView(int(i));
            VkFramebufferCreateInfo framebuffer {VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
            framebuffer.renderPass = m_renderPass;
            framebuffer.attachmentCount = 1;
            framebuffer.pAttachments = &view;
            framebuffer.width = uint32_t(m_window->swapChainImageSize().width());
            framebuffer.height = uint32_t(m_window->swapChainImageSize().height());
            framebuffer.layers = 1;
            checked(m_df->vkCreateFramebuffer(m_device, &framebuffer, nullptr, &frame.framebuffer), "framebuffer");
            VkDescriptorSetAllocateInfo allocation {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
            allocation.descriptorPool = m_descriptorPool;
            allocation.descriptorSetCount = 1;
            allocation.pSetLayouts = &m_descriptorLayout;
            checked(m_df->vkAllocateDescriptorSets(m_device, &allocation, &frame.descriptor), "descriptor set");
            if (m_auditResources) {
                VkEventCreateInfo event {VK_STRUCTURE_TYPE_EVENT_CREATE_INFO};
                checked(m_df->vkCreateEvent(m_device, &event, nullptr, &frame.completionEvent), "completion event");
            }
        }
        qCInfo(lcVulkan) << "Swapchain resource copies:" << count;
        createPipeline();
    }
    catch (const std::exception& error) { fail(QString::fromUtf8(error.what())); }
}

void FallingNotesVulkanRenderer::releaseSwapChainResources()
{
    // Qt waits for device work before releasing or recreating the swapchain.
    if (m_pipeline) m_df->vkDestroyPipeline(m_device, m_pipeline, nullptr);
    m_pipeline = VK_NULL_HANDLE;
    for (auto& frame : m_images) {
        if (frame.framebuffer) m_df->vkDestroyFramebuffer(m_device, frame.framebuffer, nullptr);
        destroy(frame.notes); destroy(frame.staticUi); destroy(frame.dynamicUi);
        destroy(frame.staging); destroy(frame.backgroundStaging);
        if (frame.imageView) m_df->vkDestroyImageView(m_device, frame.imageView, nullptr);
        if (frame.image) m_df->vkDestroyImage(m_device, frame.image, nullptr);
        if (frame.imageMemory) m_df->vkFreeMemory(m_device, frame.imageMemory, nullptr);
        if (frame.backgroundImageView) m_df->vkDestroyImageView(m_device, frame.backgroundImageView, nullptr);
        if (frame.backgroundImage) m_df->vkDestroyImage(m_device, frame.backgroundImage, nullptr);
        if (frame.backgroundImageMemory) m_df->vkFreeMemory(m_device, frame.backgroundImageMemory, nullptr);
        if (frame.completionEvent) m_df->vkDestroyEvent(m_device, frame.completionEvent, nullptr);
    }
    m_images.clear();
    if (m_renderPass) m_df->vkDestroyRenderPass(m_device, m_renderPass, nullptr);
    m_renderPass = VK_NULL_HANDLE;
    if (m_descriptorPool) m_df->vkDestroyDescriptorPool(m_device, m_descriptorPool, nullptr);
    m_descriptorPool = VK_NULL_HANDLE;
}

void FallingNotesVulkanRenderer::releaseResources()
{
    if (!m_device) return;
    // Qt waits for its device work before entering resource release callbacks.
    releaseSwapChainResources();
    if (m_sampler) m_df->vkDestroySampler(m_device, m_sampler, nullptr);
    if (m_layout) m_df->vkDestroyPipelineLayout(m_device, m_layout, nullptr);
    if (m_descriptorLayout) m_df->vkDestroyDescriptorSetLayout(m_device, m_descriptorLayout, nullptr);
    m_sampler = VK_NULL_HANDLE; m_layout = VK_NULL_HANDLE;
    m_descriptorPool = VK_NULL_HANDLE; m_descriptorLayout = VK_NULL_HANDLE;
    m_device = VK_NULL_HANDLE; m_df = nullptr;
}

void FallingNotesVulkanRenderer::createAtlas(ImageResources& frame)
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
    write.dstBinding = 1;
    m_df->vkUpdateDescriptorSets(m_device, 1, &write, 0, nullptr);
}

void FallingNotesVulkanRenderer::uploadAtlas(ImageResources& frame, VkCommandBuffer command)
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

void FallingNotesVulkanRenderer::uploadBackground(ImageResources& frame, VkCommandBuffer command)
{
    const auto& image = m_window->backgroundImage();
    const quint64 revision = m_window->backgroundRevision();
    if (frame.backgroundRevision == revision) return;

    if (frame.backgroundImageView) m_df->vkDestroyImageView(m_device, frame.backgroundImageView, nullptr);
    if (frame.backgroundImage) m_df->vkDestroyImage(m_device, frame.backgroundImage, nullptr);
    if (frame.backgroundImageMemory) m_df->vkFreeMemory(m_device, frame.backgroundImageMemory, nullptr);
    frame.backgroundImageView = VK_NULL_HANDLE;
    frame.backgroundImage = VK_NULL_HANDLE;
    frame.backgroundImageMemory = VK_NULL_HANDLE;

    if (image.isNull()) {
        if (frame.imageView) {
            VkDescriptorImageInfo descriptorInfo {m_sampler, frame.imageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
            VkWriteDescriptorSet write {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
            write.dstSet = frame.descriptor; write.dstBinding = 1; write.descriptorCount = 1;
            write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; write.pImageInfo = &descriptorInfo;
            m_df->vkUpdateDescriptorSets(m_device, 1, &write, 0, nullptr);
        }
        frame.backgroundRevision = revision;
        return;
    }

    const int width = image.width();
    const int height = image.height();
    VkImageCreateInfo info {VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    info.imageType = VK_IMAGE_TYPE_2D; info.format = VK_FORMAT_R8G8B8A8_UNORM;
    info.extent = {uint32_t(width), uint32_t(height), 1};
    info.mipLevels = info.arrayLayers = 1; info.samples = VK_SAMPLE_COUNT_1_BIT;
    info.tiling = VK_IMAGE_TILING_OPTIMAL;
    info.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    checked(m_df->vkCreateImage(m_device, &info, nullptr, &frame.backgroundImage), "background image");
    VkMemoryRequirements requirements;
    m_df->vkGetImageMemoryRequirements(m_device, frame.backgroundImage, &requirements);
    VkMemoryAllocateInfo allocation {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    allocation.allocationSize = requirements.size;
    allocation.memoryTypeIndex = memoryType(requirements.memoryTypeBits, 0, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    checked(m_df->vkAllocateMemory(m_device, &allocation, nullptr, &frame.backgroundImageMemory), "background image memory");
    checked(m_df->vkBindImageMemory(m_device, frame.backgroundImage, frame.backgroundImageMemory, 0), "bind background image");
    VkImageViewCreateInfo view {VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    view.image = frame.backgroundImage; view.viewType = VK_IMAGE_VIEW_TYPE_2D; view.format = info.format;
    view.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    checked(m_df->vkCreateImageView(m_device, &view, nullptr, &frame.backgroundImageView), "background image view");

    reserve(frame.backgroundStaging, image.sizeInBytes(), VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
    upload(frame.backgroundStaging, image.constBits(), image.sizeInBytes());
    VkImageMemoryBarrier barrier {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED; barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = frame.backgroundImage;
    barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    m_df->vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
        0, 0, nullptr, 0, nullptr, 1, &barrier);
    VkBufferImageCopy copy {};
    copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    copy.imageExtent = {uint32_t(width), uint32_t(height), 1};
    m_df->vkCmdCopyBufferToImage(command, frame.backgroundStaging.handle, frame.backgroundImage,
                                 VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL; barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    m_df->vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        0, 0, nullptr, 0, nullptr, 1, &barrier);
    VkDescriptorImageInfo descriptorInfo {m_sampler, frame.backgroundImageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkWriteDescriptorSet write {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    write.dstSet = frame.descriptor; write.dstBinding = 1; write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; write.pImageInfo = &descriptorInfo;
    m_df->vkUpdateDescriptorSets(m_device, 1, &write, 0, nullptr);
    frame.backgroundRevision = revision;
}

void FallingNotesVulkanRenderer::draw(VkCommandBuffer command, Buffer& buffer, uint32_t count, uint32_t first)
{
    if (!count) return;
    if ((VkDeviceSize(first) + count) * sizeof(VulkanQuad) > buffer.capacity) {
        fail(QStringLiteral("Vulkan instance range exceeds buffer capacity"));
        return;
    }
    const VkDeviceSize offset = 0;
    m_df->vkCmdBindVertexBuffers(command, 0, 1, &buffer.handle, &offset);
    m_df->vkCmdDraw(command, 6, count, 0, first);
}

void FallingNotesVulkanRenderer::completePresentation()
{
    // frameReady() can synchronously report device loss and release resources.
    if (m_failed || !m_device || !m_df) return;
    // QVulkanWindow in Qt 6.8 rotates its WSI semaphores by currentFrame(),
    // independently of acquired image order. Complete the submission/present
    // queue before another acquire can reuse them (VUID 01779 / 00067).
    // Qt does not expose a separate present queue, so that uncommon case must
    // wait for the device. This compatibility boundary intentionally limits
    // frames in flight; remove only with a validated Qt WSI implementation.
    const VkResult result = m_separatePresentQueue
        ? m_df->vkDeviceWaitIdle(m_device)
        : m_df->vkQueueWaitIdle(m_window->graphicsQueue());
    if (result != VK_SUCCESS)
        fail(QStringLiteral("Vulkan queue completion failed: %1").arg(result));
}

void FallingNotesVulkanRenderer::startNextFrame()
{
    VkCommandBuffer command = m_window->currentCommandBuffer();
    const auto state = m_window->sceneState();
    // A failed or lost device must not receive further render-pass commands.
    // QVulkanWindow still requires frameReady() to release the frame slot.
    if (m_failed || !m_device || !m_df || m_images.empty()) {
        m_window->frameReady();
        m_window->frameCompleted(false);
        return;
    }
    // Qt 6.8 waits for this image's draw fence before startNextFrame(). Its
    // currentFrame() fence belongs to image acquisition, not the last draw
    // using our buffers. With three images and two frame slots those lifetimes
    // diverge; per-image ownership prevents host writes racing vertex fetches.
    const auto imageIndex = size_t(m_window->currentSwapChainImageIndex());
    auto& frame = m_images.at(imageIndex);
    if (!m_failed) {
        try {
            if (frame.completionEvent) {
                if (frame.submitted && m_df->vkGetEventStatus(m_device, frame.completionEvent) != VK_EVENT_SET)
                    throw std::runtime_error("Vulkan resources reused before previous GPU draw completed");
                checked(m_df->vkResetEvent(m_device, frame.completionEvent), "reset completion event");
            }
            m_scene.prepare(state, m_window->size(), m_window->devicePixelRatio(), m_window->sceneFont(),
                            !m_window->backgroundImage().isNull(), m_window->backgroundImage().size(),
                            m_window->backgroundRevision());
            if (frame.notesRevision != m_scene.notesRevision()) {
                reserve(frame.notes, VkDeviceSize(m_scene.notes().size()) * sizeof(VulkanQuad), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
                upload(frame.notes, m_scene.notes().constData(), VkDeviceSize(m_scene.notes().size()) * sizeof(VulkanQuad));
                frame.notesRevision = m_scene.notesRevision();
            }
            if (frame.staticUiRevision != m_scene.staticUiRevision()) {
                const auto& quads = m_scene.staticUi().quads;
                const auto bytes = VkDeviceSize(quads.size()) * sizeof(VulkanQuad);
                reserve(frame.staticUi, bytes, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
                upload(frame.staticUi, quads.constData(), bytes);
                frame.staticUiRevision = m_scene.staticUiRevision();
            }
            uploadDynamicUi(frame);
            uploadAtlas(frame, command);
            uploadBackground(frame, command);
            qCDebug(lcVulkan) << "Image:" << imageIndex << "frame:" << m_window->currentFrame()
                << "static revision:" << frame.staticUiRevision << "notes revision:" << frame.notesRevision
                << "instances (static/dynamic/notes):" << m_scene.staticUi().quads.size()
                << m_scene.dynamicUi().quads.size() << m_scene.notes().size();
        } catch (const std::exception& error) { fail(QString::fromUtf8(error.what())); }
    }
    VkClearValue clear {};
    const auto background = theme::themeFor(state.themeMode).visualization.background;
    clear.color = {{float(background.redF()), float(background.greenF()), float(background.blueF()), 1}};
    const QSize physical = m_window->swapChainImageSize();
    VkRenderPassBeginInfo begin {VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    begin.renderPass = m_renderPass; begin.framebuffer = frame.framebuffer;
    begin.renderArea.extent = {uint32_t(physical.width()), uint32_t(physical.height())};
    begin.clearValueCount = 1; begin.pClearValues = &clear;
    m_df->vkCmdBeginRenderPass(command, &begin, VK_SUBPASS_CONTENTS_INLINE);
    if (!m_failed && m_pipeline) {
        const auto& g = m_scene.geometry();
        const float dpr = float(m_window->devicePixelRatio());
        FrameConstants constants {float(m_window->width()) * dpr, float(m_window->height()) * dpr,
            float((state.transportPositionUs - m_scene.timeOriginUs())/1'000'000.0), float(g.strikeLineY),
            float(g.pixelsPerMicrosecond*1'000'000), float(g.fallingRect.top()), float(g.fallingRect.bottom()), dpr,
            float(m_scene.bodyOpacity())};
        VkViewport viewport {0, 0, float(physical.width()), float(physical.height()), 0, 1};
        VkRect2D scissor {{0, 0}, begin.renderArea.extent};
        m_df->vkCmdSetViewport(command, 0, 1, &viewport);
        m_df->vkCmdSetScissor(command, 0, 1, &scissor);
        m_df->vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);
        m_df->vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_GRAPHICS, m_layout, 0, 1, &frame.descriptor, 0, nullptr);
        m_df->vkCmdPushConstants(command, m_layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(constants), &constants);
        const auto drawLayer = [&](VulkanUiLayer layer) {
            const auto base = m_scene.staticUi().range(layer);
            const auto animated = m_scene.dynamicUi().range(layer);
            draw(command, frame.staticUi, base.count, base.first);
            draw(command, frame.dynamicUi, animated.count, animated.first);
        };
        drawLayer(VulkanUiLayer::Background);
        draw(command, frame.notes, uint32_t(m_scene.notes().size()));
        for (size_t layer = size_t(VulkanUiLayer::Strike); layer < size_t(VulkanUiLayer::Count); ++layer)
            drawLayer(VulkanUiLayer(layer));
    }
    m_df->vkCmdEndRenderPass(command);
    if (frame.completionEvent)
        m_df->vkCmdSetEvent(command, frame.completionEvent, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);
    frame.submitted = true;
    m_window->frameReady();
    completePresentation();
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

void FallingNotesVulkanWindow::setShowNotationStrip(bool show)
{
    if (m_showNotationStrip == show) return;
    m_showNotationStrip = show;
    requestUpdate();
}

void FallingNotesVulkanWindow::setThemeMode(midi_play::settings::ThemeMode mode)
{
    const auto normalized = midi_play::settings::normalizeThemeMode(mode);
    if (m_themeMode == normalized) return;
    m_themeMode = normalized;
    requestUpdate();
}

void FallingNotesVulkanWindow::setNoteColorMode(midi_play::settings::NoteColorMode mode)
{
    const auto normalized = midi_play::settings::normalizeNoteColorMode(mode);
    if (m_noteColorMode == normalized) return;
    m_noteColorMode = normalized;
    requestUpdate();
}

void FallingNotesVulkanWindow::setBackgroundImage(const QImage& image)
{
    m_backgroundImage = image;
    ++m_backgroundRevision;
    requestUpdate();
}

midi_play::visualization::PlaybackSceneState FallingNotesVulkanWindow::sceneState() const
{
    midi_play::visualization::PlaybackSceneState result;
    result.chart = m_chart;
    result.transportPositionUs = m_visualClock ? m_visualClock->position() : m_positionUs;
    result.effectsStartUs = m_effectsStartUs;
    result.durationUs = m_durationUs;
    result.transportState = m_state;
    result.loading = m_loading;
    result.showNotationStrip = m_showNotationStrip;
    result.themeMode = m_themeMode;
    result.noteColorMode = m_noteColorMode;
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
