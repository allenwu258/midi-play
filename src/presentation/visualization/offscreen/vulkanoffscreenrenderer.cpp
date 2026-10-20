// 离屏导出实现：独立图形队列提交、三槽流水处理及同步 RGBA 回读。
#include <vulkan/vulkan.h>
#include "vulkanoffscreenrenderer.h"
#include "presentation/visualization/exportframing.h"
#include "presentation/visualization/vulkanscene.h"
#include "presentation/visualization/vulkanpipeline.h"
#include "infrastructure/media/videoframereader.h"
#include "domain/visualization/backgroundvideotimeline.h"
#include <QElapsedTimer>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <vector>

namespace midi_play::presentation::visualization {
namespace {
// Vulkan 失败统一抛出带操作上下文的异常，由公开接口转换为错误字符串。
void check(VkResult result, const char* operation)
{
    if (result != VK_SUCCESS)
        throw std::runtime_error(QStringLiteral("%1 (Vulkan %2)").arg(QString::fromLatin1(operation)).arg(result).toStdString());
}
// 持久映射的主机可见缓冲；一致性影响 flush/invalidate，缓存性影响回读性能。
struct Buffer {
    VkBuffer handle = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    void* mapped = nullptr;
    VkDeviceSize capacity = 0;
    bool coherent = false;
    bool cached = false;
};
// 图像、内存、视图与上传暂存缓冲的所有权集合；uploaded 追踪已有采样布局。
struct Texture {
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    Buffer staging;
    bool uploaded = false;
};

// 将设备类别转换为诊断标签，不参与设备优先级选择。
QString physicalDeviceTypeName(VkPhysicalDeviceType type)
{
    switch (type) {
    case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU: return QStringLiteral("integrated");
    case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU: return QStringLiteral("discrete");
    case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU: return QStringLiteral("virtual");
    case VK_PHYSICAL_DEVICE_TYPE_CPU: return QStringLiteral("cpu");
    default: return QStringLiteral("other");
    }
}
}

// 单线程资源所有者；每帧可变资源按槽隔离，共享纹理更新由 fence 等待保护。
class VulkanOffscreenRenderer::Impl {
public:
    ~Impl() { cleanup(); }
    void initialize(const ExportSceneConfig& config, const std::atomic_bool* canceled);
    bool beginRender(qint64 position, VulkanRenderTicket& ticket, const std::atomic_bool* canceled);
    bool completeRender(VulkanRenderTicket& ticket, QImage& frame, const std::atomic_bool* canceled);
    bool render(qint64 position, QImage& frame, const std::atomic_bool* canceled);
    VulkanRenderDiagnostics diagnostics() const { return m_diagnostics; }
private:
    // 槽位从 beginRender 成功提交到 completeRender 结束一直被占用。
    // GPU 已完成也不会自动释放，保证调用方仍可按票据顺序回读。
    struct FrameSlot {
        Texture target;
        VkFramebuffer framebuffer = VK_NULL_HANDLE;
        VkCommandBuffer command = VK_NULL_HANDLE;
        VkFence fence = VK_NULL_HANDLE;
        Buffer notes;
        Buffer staticUi;
        Buffer dynamicUi;
        Buffer readback; // 接收输出图像的紧密 RGBA 像素，优先使用 HOST_CACHED 内存。
        quint64 notesRevision = 0;
        quint64 staticUiRevision = 0;
        bool inFlight = false;
        uint32_t timestampQueryBase = 0; // 每槽两个查询：命令开始、回读命令结束。
    };
    // 查找兼容内存类型，优先满足 preferred，允许退回仅满足 required 的类型。
    uint32_t memoryType(uint32_t bits, VkMemoryPropertyFlags required, VkMemoryPropertyFlags preferred);
    // 空闲槽或初始化阶段可扩容；保持映射，最小分配 4096 字节。
    void reserve(Buffer& buffer, VkDeviceSize size, VkBufferUsageFlags usage,
                 VkMemoryPropertyFlags preferred = VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    // 写入预留容量中的数据，非一致内存显式 flush。
    void write(Buffer& buffer, const void* data, VkDeviceSize size);
    // 下列释放方法要求资源不再被在途 GPU 命令引用。
    void destroy(Buffer& buffer);
    void destroy(Texture& texture);
    // 创建单采样 RGBA8 最优布局图像，优先分配设备本地内存。
    void createImage(Texture& texture, QSize size, VkImageUsageFlags usage);
    // 将图片经暂存缓冲上传，显式转换传输与片元采样布局。
    void uploadTexture(Texture& texture, const QImage& image, VkCommandBuffer command);
    // 更新共享描述符的指定组合采样绑定，纹理上传在后续命令中完成。
    void bindTexture(const Texture& texture, uint32_t binding);
    // 绑定实例缓冲，按给定层范围绘制每实例六个顶点。
    void draw(VkCommandBuffer command, Buffer& buffer, VulkanInstanceRange range);
    // 等待在途 GPU 使用结束，但保留票据和 inFlight，以便随后按顺序回读。
    void waitForAllSlots();
    // 等待设备后按依赖顺序释放，兼容只完成部分初始化的状态。
    void cleanup();
    ExportSceneConfig m_config;
    ExportFramingPlan m_framing;
    VulkanScene m_scene;
    VkInstance m_instance = VK_NULL_HANDLE;
    uint32_t m_instanceApiVersion = VK_API_VERSION_1_0;
    VkDevice m_device = VK_NULL_HANDLE;
    VkQueue m_queue = VK_NULL_HANDLE;
    VkPhysicalDeviceMemoryProperties m_memory {};
    VkCommandPool m_pool = VK_NULL_HANDLE;
    VkRenderPass m_pass = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_descriptorLayout = VK_NULL_HANDLE;
    VkDescriptorPool m_descriptors = VK_NULL_HANDLE;
    VkDescriptorSet m_descriptor = VK_NULL_HANDLE;
    VkPipelineLayout m_layout = VK_NULL_HANDLE;
    VkPipeline m_pipeline = VK_NULL_HANDLE;
    VkSampler m_sampler = VK_NULL_HANDLE;
    VkQueryPool m_timestampQueries = VK_NULL_HANDLE;
    float m_timestampPeriod = 0.0f;
    Texture m_atlas, m_background; // 所有帧槽共享，修改内容前需等待各槽 fence。
    std::unique_ptr<media::VideoFrameReader> m_videoReader;
    qint64 m_backgroundCacheKey = 0;
    std::vector<FrameSlot> m_slots;
    quint64 m_atlasRevision = 0;
    float m_scale = 1, m_offsetX = 0, m_offsetY = 0; // 导出裁剪区等比缩放及物理像素偏移。
    VkRect2D m_scissor {};
    VulkanRenderDiagnostics m_diagnostics;
};

// 同时满足资源兼容位和必需属性；首选属性不是硬要求，保留一个可用回退类型。
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
// 调用方已保证 GPU 不再引用此缓冲；先解除映射，销毁句柄后再释放底层内存。
void VulkanOffscreenRenderer::Impl::destroy(Buffer& buffer)
{
    if (buffer.mapped) vkUnmapMemory(m_device, buffer.memory);
    if (buffer.handle) vkDestroyBuffer(m_device, buffer.handle, nullptr);
    if (buffer.memory) vkFreeMemory(m_device, buffer.memory, nullptr);
    buffer = {};
}
// 空闲槽上按需扩容；容量足够时保留地址，避免每帧重新分配和映射。
void VulkanOffscreenRenderer::Impl::reserve(Buffer& buffer, VkDeviceSize size, VkBufferUsageFlags usage,
                                            VkMemoryPropertyFlags preferred)
{
    if (buffer.handle && buffer.capacity >= size) return;
    destroy(buffer);
    VkBufferCreateInfo info {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    info.size = std::max<VkDeviceSize>(4096, size); info.usage = usage;
    check(vkCreateBuffer(m_device, &info, nullptr, &buffer.handle), "create buffer");
    VkMemoryRequirements requirements;
    vkGetBufferMemoryRequirements(m_device, buffer.handle, &requirements);
    // 上传侧首选 coherent；回读侧传入 cached，两者均允许 HOST_VISIBLE 备选。
    const auto type = memoryType(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
                                 preferred);
    VkMemoryAllocateInfo allocation {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    allocation.allocationSize = requirements.size; allocation.memoryTypeIndex = type;
    check(vkAllocateMemory(m_device, &allocation, nullptr, &buffer.memory), "allocate buffer");
    check(vkBindBufferMemory(m_device, buffer.handle, buffer.memory, 0), "bind buffer");
    check(vkMapMemory(m_device, buffer.memory, 0, VK_WHOLE_SIZE, 0, &buffer.mapped), "map buffer");
    buffer.capacity = info.size;
    buffer.coherent = (m_memory.memoryTypes[type].propertyFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) != 0;
    buffer.cached = (m_memory.memoryTypes[type].propertyFlags & VK_MEMORY_PROPERTY_HOST_CACHED_BIT) != 0;
}
// 写入已映射且容量足够的缓冲；非 coherent 内存对整个映射范围 flush，满足原子对齐要求。
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
// 图像视图依赖图像、图像依赖内存；按该依赖顺序释放并归零，兼容部分初始化失败。
void VulkanOffscreenRenderer::Impl::destroy(Texture& texture)
{
    destroy(texture.staging);
    if (texture.view) vkDestroyImageView(m_device, texture.view, nullptr);
    if (texture.image) vkDestroyImage(m_device, texture.image, nullptr);
    if (texture.memory) vkFreeMemory(m_device, texture.memory, nullptr);
    texture = {};
}
// 分步创建 RGBA8 图像、内存和视图；每一步成功句柄都留在 texture，供异常清理回收。
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
// 描述符声明采样时布局；实际布局转换由上传命令完成，更新前须等待共享资源的在途使用。
void VulkanOffscreenRenderer::Impl::bindTexture(const Texture& texture, uint32_t binding)
{
    VkDescriptorImageInfo image {m_sampler, texture.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkWriteDescriptorSet descriptor {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    descriptor.dstSet = m_descriptor; descriptor.dstBinding = binding;
    descriptor.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; descriptor.descriptorCount = 1;
    descriptor.pImageInfo = &image;
    vkUpdateDescriptorSets(m_device, 1, &descriptor, 0, nullptr);
}
// 暂存像素后记录两次布局/访问屏障，保证先完成传输写入再供片元采样。
void VulkanOffscreenRenderer::Impl::uploadTexture(Texture& texture, const QImage& image,
                                                  VkCommandBuffer command)
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
    vkCmdPipelineBarrier(command, texture.uploaded ? VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
    VkBufferImageCopy copy {};
    // 使用 QImage 的实际行跨度，避免将行尾填充误认为连续像素。
    copy.bufferRowLength = uint32_t(image.bytesPerLine() / 4);
    copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    copy.imageExtent = {uint32_t(image.width()), uint32_t(image.height()), 1};
    vkCmdCopyBufferToImage(command, texture.staging.handle, texture.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL; barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        0, 0, nullptr, 0, nullptr, 1, &barrier);
    texture.uploaded = true;
}

// 建立独立设备及离屏目标，所有成功创建的句柄由 Impl 析构统一回收。
void VulkanOffscreenRenderer::Impl::initialize(const ExportSceneConfig& config, const std::atomic_bool* canceled)
{
    if (canceled && canceled->load()) throw std::runtime_error("Video initialization canceled");
    m_diagnostics = {};
    m_diagnostics.enabled = qEnvironmentVariableIntValue("MIDI_PLAY_VIDEO_DIAGNOSTICS") > 0;
    m_config = config;
    // 在创建 GPU 资源前验证画幅，后续场景准备始终复用相同约束。
    m_framing = calculateExportFramingPlan(config.logicalSize, config.outputSize,
        config.state.chart.get(), config.state.lookAheadUs, config.state.showNotationStrip,
        config.retainMeasureGutter);
    m_scale = float(m_framing.scale);
    m_offsetX = float(m_framing.offset.x());
    m_offsetY = float(m_framing.offset.y());
    const QRect scissor = m_framing.scissorRect;
    m_scissor = {{scissor.x(), scissor.y()}, {uint32_t(scissor.width()), uint32_t(scissor.height())}};
    m_diagnostics.logicalSceneSize = m_framing.logicalSceneSize;
    m_diagnostics.logicalCropRect = m_framing.logicalCropRect;
    m_diagnostics.geometryScale = m_framing.scale;
    // 导出自行建立可见索引，不借用实时视图的候选 span；按播放态生成完整视觉效果。
    m_config.state.candidateNoteIndices = {};
    m_config.state.loading = false; m_config.state.errorMessage.clear();
    m_config.state.transportState = playback::State::Playing;
    m_config.state.effectsStartUs = 0;
    // 兼容 Vulkan 1.0 loader，同时将实例请求版本限制在本实现使用的 1.2 以内。
    if (const auto enumerateInstanceVersion = reinterpret_cast<PFN_vkEnumerateInstanceVersion>(
            vkGetInstanceProcAddr(nullptr, "vkEnumerateInstanceVersion"))) {
        enumerateInstanceVersion(&m_instanceApiVersion);
        m_instanceApiVersion = std::min(m_instanceApiVersion, uint32_t(VK_API_VERSION_1_2));
    }
    VkApplicationInfo app {VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.pApplicationName = "MIDI Play video export"; app.apiVersion = m_instanceApiVersion;
    VkInstanceCreateInfo instance {VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO}; instance.pApplicationInfo = &app;
    check(vkCreateInstance(&instance, nullptr, &m_instance), "create Vulkan instance");
    uint32_t count = 0;
    check(vkEnumeratePhysicalDevices(m_instance, &count, nullptr), "enumerate GPUs");
    std::vector<VkPhysicalDevice> devices(count);
    check(vkEnumeratePhysicalDevices(m_instance, &count, devices.data()), "enumerate GPUs");
    VkPhysicalDevice physical = VK_NULL_HANDLE;
    uint32_t family = 0;
    // 选择枚举顺序中第一个具有图形队列的设备；离屏流程不要求呈现支持。
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
    vkGetPhysicalDeviceMemoryProperties(physical, &m_memory);
    m_diagnostics.deviceName = QString::fromUtf8(properties.deviceName);
    m_diagnostics.deviceType = physicalDeviceTypeName(properties.deviceType);
    m_diagnostics.vendorId = properties.vendorID;
    m_diagnostics.driverVersion = properties.driverVersion;
    m_diagnostics.queueFamily = family;
    m_diagnostics.timestampValidBits = [&] {
        uint32_t families = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(physical, &families, nullptr);
        std::vector<VkQueueFamilyProperties> queueProperties(families);
        vkGetPhysicalDeviceQueueFamilyProperties(physical, &families, queueProperties.data());
        return family < families ? queueProperties[family].timestampValidBits : 0u;
    }();
    m_timestampPeriod = properties.limits.timestampPeriod;
    for (uint32_t i = 0; i < m_memory.memoryHeapCount; ++i) {
        if (m_memory.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT)
            m_diagnostics.deviceLocalHeapBytes += quint64(m_memory.memoryHeaps[i].size);
    }
    // 多种 HOST_VISIBLE 内存类型可能共享同一 heap，统计容量时只计一次。
    std::vector<bool> countedHostHeaps(m_memory.memoryHeapCount, false);
    for (uint32_t i = 0; i < m_memory.memoryTypeCount; ++i) {
        if (!(m_memory.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)) continue;
        const auto heap = m_memory.memoryTypes[i].heapIndex;
        if (!countedHostHeaps[heap]) {
            m_diagnostics.hostVisibleHeapBytes += quint64(m_memory.memoryHeaps[heap].size);
            countedHostHeaps[heap] = true;
        }
    }
    const uint32_t apiMajor = VK_VERSION_MAJOR(properties.apiVersion);
    const uint32_t apiMinor = VK_VERSION_MINOR(properties.apiVersion);
    // 时间戳诊断需实例/设备 1.2、队列有效位和有效周期；不满足时仅保留 CPU 诊断。
    m_diagnostics.gpuTimestamps = m_diagnostics.enabled
        && m_instanceApiVersion >= VK_API_VERSION_1_2
        && (apiMajor > 1 || (apiMajor == 1 && apiMinor >= 2))
        && m_diagnostics.timestampValidBits > 0 && m_timestampPeriod > 0.0f;
    if (uint32_t(std::max(config.outputSize.width(), config.outputSize.height())) > properties.limits.maxImageDimension2D)
        throw std::runtime_error("Output exceeds Vulkan device image limit");
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
    VkAttachmentDescription color {};
    color.format = VK_FORMAT_R8G8B8A8_UNORM; color.samples = VK_SAMPLE_COUNT_1_BIT;
    color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR; color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE; color.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    color.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED; color.finalLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    VkAttachmentReference reference {0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription subpass {}; subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1; subpass.pColorAttachments = &reference;
    // 前后依赖分别衔接前次传输读取、颜色写入和本次传输回读。
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
    // 背景转换为未预乘 RGBA；空背景复用图集视图，保证两个描述符始终有效。
    if (!config.backgroundVideoPath.isEmpty()) {
        m_videoReader = std::make_unique<media::VideoFrameReader>();
        QString error;
        if (!m_videoReader->open(config.backgroundVideoPath, config.backgroundMaximumDimension, &error, canceled))
            throw std::runtime_error(error.toStdString());
        m_config.background = m_videoReader->frameAt(0, &error).image;
        if (m_config.background.isNull()) throw std::runtime_error(error.toStdString());
    }
    if (!m_config.background.isNull()) {
        m_config.background = m_config.background.convertToFormat(QImage::Format_RGBA8888);
        createImage(m_background, m_config.background.size(), VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT);
        bindTexture(m_background, 1);
    } else bindTexture(m_atlas, 1);
    // 有界三槽允许 CPU 准备后续帧与此前 GPU 工作重叠，不无限堆积显存资源。
    constexpr int slotCount = 3;
    m_slots.resize(slotCount);
    if (m_diagnostics.gpuTimestamps) {
        VkQueryPoolCreateInfo query {VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
        query.queryType = VK_QUERY_TYPE_TIMESTAMP;
        query.queryCount = uint32_t(slotCount * 2);
        // 诊断查询池创建失败只关闭 GPU 计时，不阻止正常导出。
        if (vkCreateQueryPool(m_device, &query, nullptr, &m_timestampQueries) != VK_SUCCESS) {
            m_timestampQueries = VK_NULL_HANDLE;
            m_diagnostics.gpuTimestamps = false;
        }
    }
    std::vector<VkCommandBuffer> commands(static_cast<size_t>(slotCount));
    VkCommandBufferAllocateInfo command {VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    command.commandPool = m_pool;
    command.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    command.commandBufferCount = uint32_t(slotCount);
    check(vkAllocateCommandBuffers(m_device, &command, commands.data()), "allocate frame command buffers");
    const VkDeviceSize readbackBytes = VkDeviceSize(config.outputSize.width())
        * config.outputSize.height() * 4;
    VkFenceCreateInfo fence {VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    VkFramebufferCreateInfo framebuffer {VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
    framebuffer.renderPass = m_pass;
    framebuffer.attachmentCount = 1;
    framebuffer.width = uint32_t(config.outputSize.width());
    framebuffer.height = uint32_t(config.outputSize.height());
    framebuffer.layers = 1;
    for (int i = 0; i < slotCount; ++i) {
        auto& slot = m_slots[size_t(i)];
        slot.command = commands[size_t(i)];
        slot.timestampQueryBase = uint32_t(i * 2);
        check(vkCreateFence(m_device, &fence, nullptr, &slot.fence), "create frame fence");
        createImage(slot.target, config.outputSize,
                    VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
        framebuffer.pAttachments = &slot.target.view;
        check(vkCreateFramebuffer(m_device, &framebuffer, nullptr, &slot.framebuffer), "create frame framebuffer");
        // 上传优先 coherent 写入，回读优先 cached CPU 读取；无 cached 类型时可退回主机可见内存。
        reserve(slot.readback, readbackBytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                VK_MEMORY_PROPERTY_HOST_CACHED_BIT);
        m_diagnostics.readbackHostCached = slot.readback.cached;
    }
}

// 每个实例生成六个顶点；range.first/count 选择同一层的连续实例范围。
void VulkanOffscreenRenderer::Impl::draw(VkCommandBuffer command, Buffer& buffer,
                                         VulkanInstanceRange range)
{
    if (!range.count) return;
    const VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(command, 0, 1, &buffer.handle, &offset);
    vkCmdDraw(command, 6, range.count, 0, range.first);
}

// 等待共享纹理的旧读者结束；保留槽占用状态及票据，供调用方随后回读。
void VulkanOffscreenRenderer::Impl::waitForAllSlots()
{
    for (auto& slot : m_slots) {
        if (!slot.inFlight) continue;
        QElapsedTimer timer;
        if (m_diagnostics.enabled) timer.start();
        check(vkWaitForFences(m_device, 1, &slot.fence, VK_TRUE, 10'000'000'000ULL),
              "wait for frame slot");
        if (m_diagnostics.enabled) m_diagnostics.fenceWaitNs += quint64(timer.nsecsElapsed());
    }
}

// 为音乐时间提交一帧并发放票据；取消或无空闲槽返回 false，Vulkan 错误抛异常。
bool VulkanOffscreenRenderer::Impl::beginRender(qint64 position, VulkanRenderTicket& ticket,
                                                const std::atomic_bool* canceled)
{
    if (canceled && canceled->load()) return false;
    if (ticket.valid()) throw std::runtime_error("Render ticket is already in use");
    int slotIndex = -1;
    for (int i = 0; i < int(m_slots.size()); ++i) {
        if (!m_slots[size_t(i)].inFlight) { slotIndex = i; break; }
    }
    // 无空闲槽时交给调用方先 completeRender，不能覆盖仍持有票据的已完成帧。
    if (slotIndex < 0) return false;
    auto& slot = m_slots[size_t(slotIndex)];
    bool videoChanged = false;
    if (m_videoReader) {
        QString error;
        const auto target = midi_play::visualization::backgroundVideoPosition(position,
            m_config.state.durationUs, m_videoReader->metadata().durationUs);
        const QImage frame = m_videoReader->frameAt(target, &error, canceled).image;
        if (frame.isNull()) throw std::runtime_error(error.toStdString());
        videoChanged = frame.cacheKey() != m_backgroundCacheKey;
        if (videoChanged) {
            waitForAllSlots();
            if (frame.size() != m_config.background.size()) {
                destroy(m_background);
                createImage(m_background, frame.size(), VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT);
                bindTexture(m_background, 1);
            }
            m_config.background = frame;
            m_backgroundCacheKey = frame.cacheKey();
        }
    }
    auto state = m_config.state;
    state.transportPositionUs = position; state.updateVisibleWindow();
    QElapsedTimer timer;
    if (m_diagnostics.enabled) timer.start();
    m_scene.prepare(state, m_framing.logicalSceneSize, m_scale, m_config.font, !m_config.background.isNull(),
                    m_config.background.size(), 1, m_framing.layoutConstraints);
    if (m_diagnostics.enabled) m_diagnostics.scenePrepareNs += quint64(timer.nsecsElapsed());
    // 本帧只写入所选槽的缓冲，因此其他在途槽可继续读取自己的实例副本。
    const auto upload = [this](Buffer& buffer, const QVector<VulkanQuad>& quads) {
        const auto bytes = VkDeviceSize(quads.size()) * sizeof(VulkanQuad);
        reserve(buffer, bytes, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT); write(buffer, quads.constData(), bytes);
    };
    if (m_diagnostics.enabled) timer.restart();
    if (slot.notesRevision != m_scene.notesRevision()) {
        upload(slot.notes, m_scene.notes());
        slot.notesRevision = m_scene.notesRevision();
    }
    if (slot.staticUiRevision != m_scene.staticUiRevision()) {
        upload(slot.staticUi, m_scene.staticUi().quads);
        slot.staticUiRevision = m_scene.staticUiRevision();
    }
    upload(slot.dynamicUi, m_scene.dynamicUi().quads);
    if (m_diagnostics.enabled) m_diagnostics.bufferUploadNs += quint64(timer.nsecsElapsed());

    const bool atlasChanged = m_atlasRevision != m_scene.atlasRevision();
    const bool backgroundChanged = m_background.image && (!m_background.uploaded || videoChanged);
    // 所有槽共享纹理；覆盖图集/暂存数据前先等待旧采样命令完成，票据仍保留供顺序回读。
    if (atlasChanged || backgroundChanged) waitForAllSlots();

    if (m_diagnostics.enabled) timer.restart();
    check(vkResetCommandBuffer(slot.command, 0), "reset frame command buffer");
    VkCommandBufferBeginInfo begin {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    check(vkBeginCommandBuffer(slot.command, &begin), "begin frame command buffer");
    if (m_diagnostics.gpuTimestamps) {
        // 查询随槽复用；从本帧命令开始到图像回读及屏障结束计算 GPU 时间。
        vkCmdResetQueryPool(slot.command, m_timestampQueries, slot.timestampQueryBase, 2);
        vkCmdWriteTimestamp(slot.command, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                            m_timestampQueries, slot.timestampQueryBase);
    }
    if (atlasChanged) {
        uploadTexture(m_atlas, m_scene.atlas(), slot.command);
        m_atlasRevision = m_scene.atlasRevision();
    }
    if (backgroundChanged) uploadTexture(m_background, m_config.background, slot.command);
    const auto color = theme::themeFor(state.themeMode).visualization.background;
    VkClearValue clear {}; clear.color = {{float(color.redF()), float(color.greenF()), float(color.blueF()), 1}};
    const QSize size = m_config.outputSize;
    VkRenderPassBeginInfo pass {VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    pass.renderPass = m_pass; pass.framebuffer = slot.framebuffer;
    pass.renderArea.extent = {uint32_t(size.width()), uint32_t(size.height())};
    pass.clearValueCount = 1; pass.pClearValues = &clear;
    vkCmdBeginRenderPass(slot.command, &pass, VK_SUBPASS_CONTENTS_INLINE);
    const VkViewport viewport {0, 0, float(size.width()), float(size.height()), 0, 1};
    vkCmdSetViewport(slot.command, 0, 1, &viewport); vkCmdSetScissor(slot.command, 0, 1, &m_scissor);
    vkCmdBindPipeline(slot.command, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);
    vkCmdBindDescriptorSets(slot.command, VK_PIPELINE_BIND_POINT_GRAPHICS, m_layout, 0, 1, &m_descriptor, 0, nullptr);
    const auto& g = m_scene.geometry(); const auto& effects = m_scene.effectsProfile();
    // m_scale 作为离屏 DPR，同时使用物理像素偏移将逻辑裁剪区域居中到输出帧。
    const FrameConstants constants {float(size.width()), float(size.height()),
        float((position - m_scene.timeOriginUs()) / 1'000'000.0), float(g.strikeLineY),
        float(g.pixelsPerMicrosecond * 1'000'000), float(g.fallingRect.top()), float(g.fallingRect.bottom()), m_scale,
        float(m_scene.bodyOpacity()), effects.noteHaloStrength, effects.noteEdgeStrength, effects.noteSheenStrength,
        effects.strikeGlowStrength, effects.keyGlowStrength, effects.particleStrength,
        m_offsetX, m_offsetY, float(std::fmod(position / 1'000'000.0, 250.0))};
    vkCmdPushConstants(slot.command, m_layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(constants), &constants);
    // 与实时后端采用同一层序，每层先静态后动态，保证白键/黑键遮挡正确。
    const auto layer = [this, &slot](VulkanUiLayer layer) {
        draw(slot.command, slot.staticUi, m_scene.staticUi().range(layer));
        draw(slot.command, slot.dynamicUi, m_scene.dynamicUi().range(layer));
    };
    layer(VulkanUiLayer::Background);
    draw(slot.command, slot.notes, {0, uint32_t(m_scene.notes().size())});
    for (size_t i = size_t(VulkanUiLayer::Strike); i < size_t(VulkanUiLayer::Count); ++i) layer(VulkanUiLayer(i));
    vkCmdEndRenderPass(slot.command);
    VkBufferImageCopy copy {}; copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    copy.imageExtent = {uint32_t(size.width()), uint32_t(size.height()), 1};
    vkCmdCopyImageToBuffer(slot.command, slot.target.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, slot.readback.handle, 1, &copy);
    // GPU 传输写入必须对后续 CPU 读取可见；CPU 仍需等待 fence 并按内存属性 invalidate。
    VkBufferMemoryBarrier barrier {VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; barrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.buffer = slot.readback.handle; barrier.size = VK_WHOLE_SIZE;
    vkCmdPipelineBarrier(slot.command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT,
        0, 0, nullptr, 1, &barrier, 0, nullptr);
    if (m_diagnostics.gpuTimestamps) {
        vkCmdWriteTimestamp(slot.command, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                            m_timestampQueries, slot.timestampQueryBase + 1);
    }
    check(vkEndCommandBuffer(slot.command), "end frame command buffer");
    if (m_diagnostics.enabled) m_diagnostics.commandRecordNs += quint64(timer.nsecsElapsed());
    check(vkResetFences(m_device, 1, &slot.fence), "reset frame fence");
    VkSubmitInfo submit {VK_STRUCTURE_TYPE_SUBMIT_INFO}; submit.commandBufferCount = 1; submit.pCommandBuffers = &slot.command;
    if (m_diagnostics.enabled) timer.restart();
    check(vkQueueSubmit(m_queue, 1, &submit, slot.fence), "submit frame");
    if (m_diagnostics.enabled) m_diagnostics.queueSubmitNs += quint64(timer.nsecsElapsed());
    // 只有提交成功才占用槽并发放票据；GPU 完成不会自动清除 inFlight。
    slot.inFlight = true;
    if (m_diagnostics.enabled) ++m_diagnostics.submittedFrames;
    ticket.slot = slotIndex;
    ticket.musicPositionUs = position;
    return true;
}

// 等待票据所属槽的 GPU 工作结束，释放票据后复制像素；取消不撤销已提交的 GPU 命令。
bool VulkanOffscreenRenderer::Impl::completeRender(VulkanRenderTicket& ticket, QImage& frame,
                                                   const std::atomic_bool* canceled)
{
    if (!ticket.valid() || ticket.slot >= int(m_slots.size()))
        throw std::runtime_error("Invalid render ticket");
    auto& slot = m_slots[size_t(ticket.slot)];
    if (!slot.inFlight) throw std::runtime_error("Render ticket is no longer in flight");
    QElapsedTimer timer;
    if (m_diagnostics.enabled) timer.start();
    // fence 等待最多十秒，当前实现不会在等待过程中轮询取消。
    check(vkWaitForFences(m_device, 1, &slot.fence, VK_TRUE, 10'000'000'000ULL), "wait for frame");
    if (m_diagnostics.enabled) m_diagnostics.fenceWaitNs += quint64(timer.nsecsElapsed());
    // GPU 已完成后先释放槽与票据；取消仅跳过像素回读，不中断已提交命令。
    slot.inFlight = false;
    ticket.slot = -1;
    if (canceled && canceled->load()) return false;
    if (m_diagnostics.enabled) timer.restart();
    // 非一致内存需让 CPU 缓存失效；HOST_CACHED 属性不能代替此可见性处理。
    if (!slot.readback.coherent) {
        VkMappedMemoryRange range {VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE};
        range.memory = slot.readback.memory; range.size = VK_WHOLE_SIZE;
        check(vkInvalidateMappedMemoryRanges(m_device, 1, &range), "invalidate readback");
    }
    if (m_diagnostics.enabled) m_diagnostics.readbackNs += quint64(timer.nsecsElapsed());
    if (m_diagnostics.gpuTimestamps) {
        quint64 timestamps[2] = {};
        const VkResult result = vkGetQueryPoolResults(m_device, m_timestampQueries,
            slot.timestampQueryBase, 2, sizeof(timestamps), timestamps, sizeof(quint64), VK_QUERY_RESULT_64_BIT);
        // 查询失败或时间戳倒序时不累计，诊断失败不影响输出帧。
        if (result == VK_SUCCESS && timestamps[1] >= timestamps[0]) {
            m_diagnostics.gpuFrameNs += quint64(double(timestamps[1] - timestamps[0]) * double(m_timestampPeriod));
        }
    }
    const QSize size = m_config.outputSize;
    if (m_diagnostics.enabled) timer.restart();
    // 复用尺寸/格式相同的 QImage；像素复制后帧不再借用 GPU 映射内存。
    if (frame.size() != size || frame.format() != QImage::Format_RGBA8888) frame = QImage(size, QImage::Format_RGBA8888);
    if (frame.isNull()) throw std::runtime_error("Cannot allocate video frame");
    std::memcpy(frame.bits(), slot.readback.mapped, size_t(size.width()) * size.height() * 4);
    if (m_diagnostics.enabled) {
        m_diagnostics.qimageCopyNs += quint64(timer.nsecsElapsed());
        ++m_diagnostics.completedFrames;
    }
    return true;
}

// 同步组合路径，适用于调用方无需交错提交与回读的场景。
bool VulkanOffscreenRenderer::Impl::render(qint64 position, QImage& frame, const std::atomic_bool* canceled)
{
    VulkanRenderTicket ticket;
    if (!beginRender(position, ticket, canceled)) return false;
    return completeRender(ticket, frame, canceled);
}
// 析构清理也覆盖初始化中途失败，先等待设备空闲再按资源依赖释放。
void VulkanOffscreenRenderer::Impl::cleanup()
{
    if (m_device) {
        // 独立设备由本实现管理，销毁前必须等待所有已提交槽，不依赖 Qt 窗口回调。
        vkDeviceWaitIdle(m_device);
        if (m_pipeline) vkDestroyPipeline(m_device, m_pipeline, nullptr);
        for (auto& slot : m_slots) {
            if (slot.framebuffer) vkDestroyFramebuffer(m_device, slot.framebuffer, nullptr);
            if (slot.fence) vkDestroyFence(m_device, slot.fence, nullptr);
            destroy(slot.target);
            destroy(slot.notes); destroy(slot.staticUi); destroy(slot.dynamicUi); destroy(slot.readback);
        }
        m_slots.clear();
        if (m_timestampQueries) vkDestroyQueryPool(m_device, m_timestampQueries, nullptr);
        m_timestampQueries = VK_NULL_HANDLE;
        destroy(m_atlas); destroy(m_background);
        if (m_sampler) vkDestroySampler(m_device, m_sampler, nullptr);
        if (m_layout) vkDestroyPipelineLayout(m_device, m_layout, nullptr);
        if (m_descriptors) vkDestroyDescriptorPool(m_device, m_descriptors, nullptr);
        if (m_descriptorLayout) vkDestroyDescriptorSetLayout(m_device, m_descriptorLayout, nullptr);
        if (m_pass) vkDestroyRenderPass(m_device, m_pass, nullptr);
        if (m_pool) vkDestroyCommandPool(m_device, m_pool, nullptr);
        vkDestroyDevice(m_device, nullptr);
    }
    if (m_instance) vkDestroyInstance(m_instance, nullptr);
}
VulkanOffscreenRenderer::VulkanOffscreenRenderer() = default;
VulkanOffscreenRenderer::~VulkanOffscreenRenderer() = default;
// 公开包装层负责异常转错误；初始化失败销毁部分构建的实现。
bool VulkanOffscreenRenderer::initialize(const ExportSceneConfig& config, QString* error,
                                        const std::atomic_bool* canceled)
{
    try { m_impl = std::make_unique<Impl>(); m_impl->initialize(config, canceled); return true; }
    catch (const std::exception& e) { *error = QString::fromUtf8(e.what()); m_impl.reset(); return false; }
}
// 对外提交接口；调用方提供有效 error 指针，并区分取消/槽满与异常错误。
bool VulkanOffscreenRenderer::beginRender(qint64 musicPositionUs, VulkanRenderTicket& ticket,
                                          const std::atomic_bool* canceled, QString* error)
{
    if (!m_impl) { *error = QStringLiteral("离屏渲染器尚未初始化"); return false; }
    try {
        if (!m_impl->beginRender(musicPositionUs, ticket, canceled)) return false;
        return true;
    } catch (const std::exception& e) { *error = QString::fromUtf8(e.what()); return false; }
}
// 将已提交票据转换为独立 QImage；异常通过 error 返回，调用方负责停止该次导出。
bool VulkanOffscreenRenderer::completeRender(VulkanRenderTicket& ticket, QImage& frame,
                                             const std::atomic_bool* canceled, QString* error)
{
    if (!m_impl) { *error = QStringLiteral("离屏渲染器尚未初始化"); return false; }
    try { return m_impl->completeRender(ticket, frame, canceled); }
    catch (const std::exception& e) { *error = QString::fromUtf8(e.what()); return false; }
}
// 对外同步入口，组合提交与回读并把异常转换为错误文本。
bool VulkanOffscreenRenderer::render(qint64 position, QImage& frame, const std::atomic_bool* canceled, QString* error)
{
    if (!m_impl) { *error = QStringLiteral("离屏渲染器尚未初始化"); return false; }
    try { return m_impl->render(position, frame, canceled); }
    catch (const std::exception& e) { *error = QString::fromUtf8(e.what()); return false; }
}
VulkanRenderDiagnostics VulkanOffscreenRenderer::diagnostics() const
{
    return m_impl ? m_impl->diagnostics() : VulkanRenderDiagnostics{};
}
} // namespace midi_play::presentation::visualization
