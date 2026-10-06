#include <vulkan/vulkan.h>
#include "vulkanoffscreenrenderer.h"
#include "presentation/visualization/vulkanscene.h"
#include "presentation/visualization/vulkanpipeline.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <vector>

namespace midi_play::presentation::visualization {
namespace {
void check(VkResult result, const char* operation)
{
    if (result != VK_SUCCESS)
        throw std::runtime_error(QStringLiteral("%1 (Vulkan %2)").arg(QString::fromLatin1(operation)).arg(result).toStdString());
}
struct Buffer {
    VkBuffer handle = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    void* mapped = nullptr;
    VkDeviceSize capacity = 0;
    bool coherent = false;
};
struct Texture {
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    Buffer staging;
    bool uploaded = false;
};
}

class VulkanOffscreenRenderer::Impl {
public:
    ~Impl() { cleanup(); }
    void initialize(const ExportSceneConfig& config);
    bool render(qint64 position, QImage& frame, const std::atomic_bool* canceled);
private:
    uint32_t memoryType(uint32_t bits, VkMemoryPropertyFlags required, VkMemoryPropertyFlags preferred);
    void reserve(Buffer& buffer, VkDeviceSize size, VkBufferUsageFlags usage);
    void write(Buffer& buffer, const void* data, VkDeviceSize size);
    void destroy(Buffer& buffer);
    void destroy(Texture& texture);
    void createImage(Texture& texture, QSize size, VkImageUsageFlags usage);
    void uploadTexture(Texture& texture, const QImage& image);
    void bindTexture(const Texture& texture, uint32_t binding);
    void draw(Buffer& buffer, VulkanInstanceRange range);
    void cleanup();
    ExportSceneConfig m_config;
    VulkanScene m_scene;
    VkInstance m_instance = VK_NULL_HANDLE;
    VkDevice m_device = VK_NULL_HANDLE;
    VkQueue m_queue = VK_NULL_HANDLE;
    VkPhysicalDeviceMemoryProperties m_memory {};
    VkCommandPool m_pool = VK_NULL_HANDLE;
    VkCommandBuffer m_command = VK_NULL_HANDLE;
    VkFence m_fence = VK_NULL_HANDLE;
    VkRenderPass m_pass = VK_NULL_HANDLE;
    VkFramebuffer m_framebuffer = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_descriptorLayout = VK_NULL_HANDLE;
    VkDescriptorPool m_descriptors = VK_NULL_HANDLE;
    VkDescriptorSet m_descriptor = VK_NULL_HANDLE;
    VkPipelineLayout m_layout = VK_NULL_HANDLE;
    VkPipeline m_pipeline = VK_NULL_HANDLE;
    VkSampler m_sampler = VK_NULL_HANDLE;
    Texture m_target, m_atlas, m_background;
    Buffer m_notes, m_staticUi, m_dynamicUi, m_readback;
    quint64 m_notesRevision = 0, m_staticRevision = 0, m_atlasRevision = 0;
    float m_scale = 1, m_offsetX = 0, m_offsetY = 0;
    VkRect2D m_scissor {};
};

uint32_t VulkanOffscreenRenderer::Impl::memoryType(uint32_t bits, VkMemoryPropertyFlags required,
                                                  VkMemoryPropertyFlags preferred)
{
    uint32_t fallback = UINT32_MAX;
    for (uint32_t i = 0; i < m_memory.memoryTypeCount; ++i) {
        const auto flags = m_memory.memoryTypes[i].propertyFlags;
        if (!(bits & (1u << i)) || (flags & required) != required) continue;
        if ((flags & preferred) == preferred) return i;
        fallback = i;
    }
    if (fallback == UINT32_MAX) throw std::runtime_error("No suitable Vulkan memory type");
    return fallback;
}
void VulkanOffscreenRenderer::Impl::destroy(Buffer& buffer)
{
    if (buffer.mapped) vkUnmapMemory(m_device, buffer.memory);
    if (buffer.handle) vkDestroyBuffer(m_device, buffer.handle, nullptr);
    if (buffer.memory) vkFreeMemory(m_device, buffer.memory, nullptr);
    buffer = {};
}
void VulkanOffscreenRenderer::Impl::reserve(Buffer& buffer, VkDeviceSize size, VkBufferUsageFlags usage)
{
    if (buffer.handle && buffer.capacity >= size) return;
    destroy(buffer);
    VkBufferCreateInfo info {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    info.size = std::max<VkDeviceSize>(4096, size); info.usage = usage;
    check(vkCreateBuffer(m_device, &info, nullptr, &buffer.handle), "create buffer");
    VkMemoryRequirements requirements;
    vkGetBufferMemoryRequirements(m_device, buffer.handle, &requirements);
    const auto type = memoryType(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
                                 VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    VkMemoryAllocateInfo allocation {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    allocation.allocationSize = requirements.size; allocation.memoryTypeIndex = type;
    check(vkAllocateMemory(m_device, &allocation, nullptr, &buffer.memory), "allocate buffer");
    check(vkBindBufferMemory(m_device, buffer.handle, buffer.memory, 0), "bind buffer");
    check(vkMapMemory(m_device, buffer.memory, 0, VK_WHOLE_SIZE, 0, &buffer.mapped), "map buffer");
    buffer.capacity = info.size;
    buffer.coherent = (m_memory.memoryTypes[type].propertyFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) != 0;
}
void VulkanOffscreenRenderer::Impl::write(Buffer& buffer, const void* data, VkDeviceSize size)
{
    if (!size) return;
    std::memcpy(buffer.mapped, data, size_t(size));
    if (!buffer.coherent) {
        VkMappedMemoryRange range {VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE};
        range.memory = buffer.memory; range.size = VK_WHOLE_SIZE;
        check(vkFlushMappedMemoryRanges(m_device, 1, &range), "flush buffer");
    }
}
void VulkanOffscreenRenderer::Impl::destroy(Texture& texture)
{
    destroy(texture.staging);
    if (texture.view) vkDestroyImageView(m_device, texture.view, nullptr);
    if (texture.image) vkDestroyImage(m_device, texture.image, nullptr);
    if (texture.memory) vkFreeMemory(m_device, texture.memory, nullptr);
    texture = {};
}
void VulkanOffscreenRenderer::Impl::createImage(Texture& texture, QSize size, VkImageUsageFlags usage)
{
    VkImageCreateInfo info {VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    info.imageType = VK_IMAGE_TYPE_2D; info.format = VK_FORMAT_R8G8B8A8_UNORM;
    info.extent = {uint32_t(size.width()), uint32_t(size.height()), 1};
    info.mipLevels = info.arrayLayers = 1; info.samples = VK_SAMPLE_COUNT_1_BIT;
    info.tiling = VK_IMAGE_TILING_OPTIMAL; info.usage = usage;
    check(vkCreateImage(m_device, &info, nullptr, &texture.image), "create image");
    VkMemoryRequirements requirements;
    vkGetImageMemoryRequirements(m_device, texture.image, &requirements);
    VkMemoryAllocateInfo allocation {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    allocation.allocationSize = requirements.size;
    allocation.memoryTypeIndex = memoryType(requirements.memoryTypeBits, 0, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    check(vkAllocateMemory(m_device, &allocation, nullptr, &texture.memory), "allocate image");
    check(vkBindImageMemory(m_device, texture.image, texture.memory, 0), "bind image");
    VkImageViewCreateInfo view {VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    view.image = texture.image; view.viewType = VK_IMAGE_VIEW_TYPE_2D; view.format = info.format;
    view.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    check(vkCreateImageView(m_device, &view, nullptr, &texture.view), "create image view");
}
void VulkanOffscreenRenderer::Impl::bindTexture(const Texture& texture, uint32_t binding)
{
    VkDescriptorImageInfo image {m_sampler, texture.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkWriteDescriptorSet descriptor {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    descriptor.dstSet = m_descriptor; descriptor.dstBinding = binding;
    descriptor.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; descriptor.descriptorCount = 1;
    descriptor.pImageInfo = &image;
    vkUpdateDescriptorSets(m_device, 1, &descriptor, 0, nullptr);
}
void VulkanOffscreenRenderer::Impl::uploadTexture(Texture& texture, const QImage& image)
{
    reserve(texture.staging, image.sizeInBytes(), VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
    write(texture.staging, image.constBits(), image.sizeInBytes());
    VkImageMemoryBarrier barrier {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    barrier.srcAccessMask = texture.uploaded ? VK_ACCESS_SHADER_READ_BIT : 0;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.oldLayout = texture.uploaded ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = texture.image; barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCmdPipelineBarrier(m_command, texture.uploaded ? VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
    VkBufferImageCopy copy {};
    copy.bufferRowLength = uint32_t(image.bytesPerLine() / 4);
    copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    copy.imageExtent = {uint32_t(image.width()), uint32_t(image.height()), 1};
    vkCmdCopyBufferToImage(m_command, texture.staging.handle, texture.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL; barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    vkCmdPipelineBarrier(m_command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        0, 0, nullptr, 0, nullptr, 1, &barrier);
    texture.uploaded = true;
}

void VulkanOffscreenRenderer::Impl::initialize(const ExportSceneConfig& config)
{
    m_config = config;
    m_config.state.candidateNoteIndices = {};
    m_config.state.loading = false; m_config.state.errorMessage.clear();
    m_config.state.transportState = playback::State::Playing;
    m_config.state.effectsStartUs = 0;
    VkApplicationInfo app {VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.pApplicationName = "MIDI Play video export"; app.apiVersion = VK_API_VERSION_1_0;
    VkInstanceCreateInfo instance {VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO}; instance.pApplicationInfo = &app;
    check(vkCreateInstance(&instance, nullptr, &m_instance), "create Vulkan instance");
    uint32_t count = 0;
    check(vkEnumeratePhysicalDevices(m_instance, &count, nullptr), "enumerate GPUs");
    std::vector<VkPhysicalDevice> devices(count);
    check(vkEnumeratePhysicalDevices(m_instance, &count, devices.data()), "enumerate GPUs");
    VkPhysicalDevice physical = VK_NULL_HANDLE;
    uint32_t family = 0;
    for (auto candidate : devices) {
        uint32_t families = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(candidate, &families, nullptr);
        std::vector<VkQueueFamilyProperties> queues(families);
        vkGetPhysicalDeviceQueueFamilyProperties(candidate, &families, queues.data());
        for (uint32_t i = 0; i < families; ++i) {
            if (queues[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) { physical = candidate; family = i; break; }
        }
        if (physical) break;
    }
    if (!physical) throw std::runtime_error("No Vulkan graphics device available");
    VkPhysicalDeviceProperties properties;
    vkGetPhysicalDeviceProperties(physical, &properties);
    if (uint32_t(std::max(config.outputSize.width(), config.outputSize.height())) > properties.limits.maxImageDimension2D)
        throw std::runtime_error("Output exceeds Vulkan device image limit");
    vkGetPhysicalDeviceMemoryProperties(physical, &m_memory);
    const float priority = 1;
    VkDeviceQueueCreateInfo queue {VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    queue.queueFamilyIndex = family; queue.queueCount = 1; queue.pQueuePriorities = &priority;
    VkDeviceCreateInfo device {VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    device.queueCreateInfoCount = 1; device.pQueueCreateInfos = &queue;
    check(vkCreateDevice(physical, &device, nullptr, &m_device), "create Vulkan device");
    vkGetDeviceQueue(m_device, family, 0, &m_queue);
    VkCommandPoolCreateInfo pool {VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    pool.queueFamilyIndex = family; pool.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    check(vkCreateCommandPool(m_device, &pool, nullptr, &m_pool), "create command pool");
    VkCommandBufferAllocateInfo command {VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    command.commandPool = m_pool; command.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; command.commandBufferCount = 1;
    check(vkAllocateCommandBuffers(m_device, &command, &m_command), "allocate command buffer");
    VkFenceCreateInfo fence {VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    check(vkCreateFence(m_device, &fence, nullptr, &m_fence), "create fence");
    VkAttachmentDescription color {};
    color.format = VK_FORMAT_R8G8B8A8_UNORM; color.samples = VK_SAMPLE_COUNT_1_BIT;
    color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR; color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE; color.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    color.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED; color.finalLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    VkAttachmentReference reference {0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription subpass {}; subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1; subpass.pColorAttachments = &reference;
    VkSubpassDependency dependencies[2] {};
    dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL; dependencies[0].dstSubpass = 0;
    dependencies[0].srcStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT;
    dependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependencies[0].srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    dependencies[1].srcSubpass = 0; dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependencies[1].dstStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT;
    dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    dependencies[1].dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    VkRenderPassCreateInfo pass {VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    pass.attachmentCount = 1; pass.pAttachments = &color; pass.subpassCount = 1; pass.pSubpasses = &subpass;
    pass.dependencyCount = 2; pass.pDependencies = dependencies;
    check(vkCreateRenderPass(m_device, &pass, nullptr, &m_pass), "create render pass");
    createImage(m_target, config.outputSize, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
    VkFramebufferCreateInfo framebuffer {VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
    framebuffer.renderPass = m_pass; framebuffer.attachmentCount = 1; framebuffer.pAttachments = &m_target.view;
    framebuffer.width = uint32_t(config.outputSize.width()); framebuffer.height = uint32_t(config.outputSize.height());
    framebuffer.layers = 1;
    check(vkCreateFramebuffer(m_device, &framebuffer, nullptr, &m_framebuffer), "create framebuffer");
    VkDescriptorSetLayoutBinding bindings[2] {};
    for (uint32_t i = 0; i < 2; ++i) {
        bindings[i].binding = i; bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[i].descriptorCount = 1; bindings[i].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    }
    VkDescriptorSetLayoutCreateInfo descriptor {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    descriptor.bindingCount = 2; descriptor.pBindings = bindings;
    check(vkCreateDescriptorSetLayout(m_device, &descriptor, nullptr, &m_descriptorLayout), "descriptor layout");
    VkDescriptorPoolSize poolSize {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2};
    VkDescriptorPoolCreateInfo descriptors {VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    descriptors.maxSets = 1; descriptors.poolSizeCount = 1; descriptors.pPoolSizes = &poolSize;
    check(vkCreateDescriptorPool(m_device, &descriptors, nullptr, &m_descriptors), "descriptor pool");
    VkDescriptorSetAllocateInfo allocation {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    allocation.descriptorPool = m_descriptors; allocation.descriptorSetCount = 1; allocation.pSetLayouts = &m_descriptorLayout;
    check(vkAllocateDescriptorSets(m_device, &allocation, &m_descriptor), "descriptor set");
    VkSamplerCreateInfo sampler {VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    sampler.magFilter = sampler.minFilter = VK_FILTER_LINEAR;
    sampler.addressModeU = sampler.addressModeV = sampler.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    check(vkCreateSampler(m_device, &sampler, nullptr, &m_sampler), "sampler");
    VkPushConstantRange constants {VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(FrameConstants)};
    VkPipelineLayoutCreateInfo layout {VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    layout.setLayoutCount = 1; layout.pSetLayouts = &m_descriptorLayout;
    layout.pushConstantRangeCount = 1; layout.pPushConstantRanges = &constants;
    check(vkCreatePipelineLayout(m_device, &layout, nullptr, &m_layout), "pipeline layout");
    m_pipeline = createScenePipeline(m_device, m_layout, m_pass);
    createImage(m_atlas, {2048, 2048}, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT);
    bindTexture(m_atlas, 0);
    if (!config.background.isNull()) {
        m_config.background = config.background.convertToFormat(QImage::Format_RGBA8888);
        createImage(m_background, m_config.background.size(), VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT);
        bindTexture(m_background, 1);
    } else bindTexture(m_atlas, 1);
    reserve(m_readback, VkDeviceSize(config.outputSize.width()) * config.outputSize.height() * 4, VK_BUFFER_USAGE_TRANSFER_DST_BIT);
    const auto geometry = SceneLayoutEngine().layout(config.logicalSize, config.state.chart.get(),
        config.state.lookAheadUs, config.state.showNotationStrip);
    const qreal left = geometry.pianoRect.isEmpty() ? geometry.drumRect.left() : geometry.pianoRect.left();
    const qreal right = geometry.drumRect.isEmpty() ? geometry.pianoRect.right() : geometry.drumRect.right();
    const QRectF crop(left, 0, right - left, geometry.keyboardRect.bottom());
    if (crop.isEmpty()) throw std::runtime_error("Empty export scene");
    m_scale = float(std::min(config.outputSize.width() / crop.width(), config.outputSize.height() / crop.height()));
    const qreal marginX = (config.outputSize.width() - crop.width() * m_scale) / 2;
    const qreal marginY = (config.outputSize.height() - crop.height() * m_scale) / 2;
    m_offsetX = float(marginX - crop.left() * m_scale); m_offsetY = float(marginY);
    const QRect scissor = QRectF(marginX, marginY, crop.width() * m_scale, crop.height() * m_scale)
        .toAlignedRect().intersected(QRect(QPoint(), config.outputSize));
    m_scissor = {{scissor.x(), scissor.y()}, {uint32_t(scissor.width()), uint32_t(scissor.height())}};
}

void VulkanOffscreenRenderer::Impl::draw(Buffer& buffer, VulkanInstanceRange range)
{
    if (!range.count) return;
    const VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(m_command, 0, 1, &buffer.handle, &offset);
    vkCmdDraw(m_command, 6, range.count, 0, range.first);
}
bool VulkanOffscreenRenderer::Impl::render(qint64 position, QImage& frame, const std::atomic_bool* canceled)
{
    if (canceled && canceled->load()) return false;
    auto state = m_config.state;
    state.transportPositionUs = position; state.updateVisibleWindow();
    m_scene.prepare(state, m_config.logicalSize, m_scale, m_config.font, !m_config.background.isNull(),
                    m_config.background.size(), 1);
    const auto upload = [this](Buffer& buffer, const QVector<VulkanQuad>& quads) {
        const auto bytes = VkDeviceSize(quads.size()) * sizeof(VulkanQuad);
        reserve(buffer, bytes, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT); write(buffer, quads.constData(), bytes);
    };
    if (m_notesRevision != m_scene.notesRevision()) { upload(m_notes, m_scene.notes()); m_notesRevision = m_scene.notesRevision(); }
    if (m_staticRevision != m_scene.staticUiRevision()) { upload(m_staticUi, m_scene.staticUi().quads); m_staticRevision = m_scene.staticUiRevision(); }
    upload(m_dynamicUi, m_scene.dynamicUi().quads);
    check(vkResetCommandBuffer(m_command, 0), "reset command buffer");
    VkCommandBufferBeginInfo begin {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    check(vkBeginCommandBuffer(m_command, &begin), "begin command buffer");
    if (m_atlasRevision != m_scene.atlasRevision()) {
        uploadTexture(m_atlas, m_scene.atlas()); m_atlasRevision = m_scene.atlasRevision();
    }
    if (m_background.image && !m_background.uploaded) uploadTexture(m_background, m_config.background);
    const auto color = theme::themeFor(state.themeMode).visualization.background;
    VkClearValue clear {}; clear.color = {{float(color.redF()), float(color.greenF()), float(color.blueF()), 1}};
    const QSize size = m_config.outputSize;
    VkRenderPassBeginInfo pass {VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    pass.renderPass = m_pass; pass.framebuffer = m_framebuffer;
    pass.renderArea.extent = {uint32_t(size.width()), uint32_t(size.height())};
    pass.clearValueCount = 1; pass.pClearValues = &clear;
    vkCmdBeginRenderPass(m_command, &pass, VK_SUBPASS_CONTENTS_INLINE);
    const VkViewport viewport {0, 0, float(size.width()), float(size.height()), 0, 1};
    vkCmdSetViewport(m_command, 0, 1, &viewport); vkCmdSetScissor(m_command, 0, 1, &m_scissor);
    vkCmdBindPipeline(m_command, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);
    vkCmdBindDescriptorSets(m_command, VK_PIPELINE_BIND_POINT_GRAPHICS, m_layout, 0, 1, &m_descriptor, 0, nullptr);
    const auto& g = m_scene.geometry(); const auto& effects = m_scene.effectsProfile();
    const FrameConstants constants {float(size.width()), float(size.height()),
        float((position - m_scene.timeOriginUs()) / 1'000'000.0), float(g.strikeLineY),
        float(g.pixelsPerMicrosecond * 1'000'000), float(g.fallingRect.top()), float(g.fallingRect.bottom()), m_scale,
        float(m_scene.bodyOpacity()), effects.noteHaloStrength, effects.noteEdgeStrength, effects.noteSheenStrength,
        effects.strikeGlowStrength, effects.keyGlowStrength, effects.particleStrength,
        m_offsetX, m_offsetY, float(std::fmod(position / 1'000'000.0, 250.0))};
    vkCmdPushConstants(m_command, m_layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(constants), &constants);
    const auto layer = [this](VulkanUiLayer layer) {
        draw(m_staticUi, m_scene.staticUi().range(layer)); draw(m_dynamicUi, m_scene.dynamicUi().range(layer));
    };
    layer(VulkanUiLayer::Background); draw(m_notes, {0, uint32_t(m_scene.notes().size())});
    for (size_t i = size_t(VulkanUiLayer::Strike); i < size_t(VulkanUiLayer::Count); ++i) layer(VulkanUiLayer(i));
    vkCmdEndRenderPass(m_command);
    VkBufferImageCopy copy {}; copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    copy.imageExtent = {uint32_t(size.width()), uint32_t(size.height()), 1};
    vkCmdCopyImageToBuffer(m_command, m_target.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, m_readback.handle, 1, &copy);
    VkBufferMemoryBarrier barrier {VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; barrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.buffer = m_readback.handle; barrier.size = VK_WHOLE_SIZE;
    vkCmdPipelineBarrier(m_command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT,
        0, 0, nullptr, 1, &barrier, 0, nullptr);
    check(vkEndCommandBuffer(m_command), "end command buffer");
    check(vkResetFences(m_device, 1, &m_fence), "reset fence");
    VkSubmitInfo submit {VK_STRUCTURE_TYPE_SUBMIT_INFO}; submit.commandBufferCount = 1; submit.pCommandBuffers = &m_command;
    check(vkQueueSubmit(m_queue, 1, &submit, m_fence), "submit frame");
    check(vkWaitForFences(m_device, 1, &m_fence, VK_TRUE, 10'000'000'000ULL), "wait for frame");
    if (canceled && canceled->load()) return false;
    if (!m_readback.coherent) {
        VkMappedMemoryRange range {VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE}; range.memory = m_readback.memory; range.size = VK_WHOLE_SIZE;
        check(vkInvalidateMappedMemoryRanges(m_device, 1, &range), "invalidate readback");
    }
    if (frame.size() != size || frame.format() != QImage::Format_RGBA8888) frame = QImage(size, QImage::Format_RGBA8888);
    if (frame.isNull()) throw std::runtime_error("Cannot allocate video frame");
    std::memcpy(frame.bits(), m_readback.mapped, size_t(size.width()) * size.height() * 4);
    return true;
}
void VulkanOffscreenRenderer::Impl::cleanup()
{
    if (m_device) {
        vkDeviceWaitIdle(m_device);
        if (m_pipeline) vkDestroyPipeline(m_device, m_pipeline, nullptr);
        if (m_framebuffer) vkDestroyFramebuffer(m_device, m_framebuffer, nullptr);
        destroy(m_target); destroy(m_atlas); destroy(m_background);
        destroy(m_notes); destroy(m_staticUi); destroy(m_dynamicUi); destroy(m_readback);
        if (m_sampler) vkDestroySampler(m_device, m_sampler, nullptr);
        if (m_layout) vkDestroyPipelineLayout(m_device, m_layout, nullptr);
        if (m_descriptors) vkDestroyDescriptorPool(m_device, m_descriptors, nullptr);
        if (m_descriptorLayout) vkDestroyDescriptorSetLayout(m_device, m_descriptorLayout, nullptr);
        if (m_pass) vkDestroyRenderPass(m_device, m_pass, nullptr);
        if (m_fence) vkDestroyFence(m_device, m_fence, nullptr);
        if (m_pool) vkDestroyCommandPool(m_device, m_pool, nullptr);
        vkDestroyDevice(m_device, nullptr);
    }
    if (m_instance) vkDestroyInstance(m_instance, nullptr);
}
VulkanOffscreenRenderer::VulkanOffscreenRenderer() = default;
VulkanOffscreenRenderer::~VulkanOffscreenRenderer() = default;
bool VulkanOffscreenRenderer::initialize(const ExportSceneConfig& config, QString* error)
{
    try { m_impl = std::make_unique<Impl>(); m_impl->initialize(config); return true; }
    catch (const std::exception& e) { *error = QString::fromUtf8(e.what()); m_impl.reset(); return false; }
}
bool VulkanOffscreenRenderer::render(qint64 position, QImage& frame, const std::atomic_bool* canceled, QString* error)
{
    if (!m_impl) { *error = QStringLiteral("离屏渲染器尚未初始化"); return false; }
    try { return m_impl->render(position, frame, canceled); }
    catch (const std::exception& e) { *error = QString::fromUtf8(e.what()); return false; }
}
} // namespace midi_play::presentation::visualization
