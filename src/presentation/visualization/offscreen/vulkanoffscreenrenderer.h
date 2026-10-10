#pragma once
// 视频导出的离屏 Vulkan 接口：在独立设备上渲染音乐时间对应的 RGBA 帧。
#include "domain/visualization/playbackscenestate.h"
#include <QFont>
#include <QImage>
#include <memory>
#include <atomic>

namespace midi_play::presentation::visualization {
// 导出期间持有的场景快照；逻辑布局尺寸与最终输出物理尺寸分开配置。
struct ExportSceneConfig {
    // 导出保存设置和图表；VulkanScene 会自行查询候选，不使用 state 中借用的候选 span。
    midi_play::visualization::PlaybackSceneState state;
    QSize logicalSize {1280, 720}; // 键盘/落音布局的逻辑像素尺寸。
    QSize outputSize {1920, 1080}; // 编码帧的物理像素尺寸。
    QFont font;
    QImage background; // 按值保存源图像，离屏初始化时独立上传到设备纹理。
};

// MIDI_PLAY_VIDEO_DIAGNOSTICS 开启时累计当前渲染器的阶段耗时及帧计数。
// 禁用时计时/帧计数保持为零，设备与内存元信息仍会采集。
struct VulkanRenderDiagnostics {
    bool enabled = false;
    bool gpuTimestamps = false;
    QString deviceName;
    QString deviceType;
    quint32 vendorId = 0;
    quint32 driverVersion = 0;
    quint32 queueFamily = 0;
    quint32 timestampValidBits = 0; // 所选图形队列支持的时间戳有效位数。
    quint64 deviceLocalHeapBytes = 0;
    quint64 hostVisibleHeapBytes = 0;
    bool readbackHostCached = false;
    // CPU 阶段累计纳秒；readbackNs 记录映射缓存失效处理，不包含 QImage 复制。
    quint64 scenePrepareNs = 0;
    quint64 bufferUploadNs = 0;
    quint64 commandRecordNs = 0;
    quint64 queueSubmitNs = 0;
    quint64 fenceWaitNs = 0;
    quint64 readbackNs = 0;
    quint64 qimageCopyNs = 0;
    quint64 gpuFrameNs = 0; // GPU 时间戳覆盖纹理上传、绘制及图像回读命令。
    qint64 submittedFrames = 0;
    qint64 completedFrames = 0;
};

// 成功提交的帧占有一个槽位，直到 completeRender 等待并释放该槽。
// 票据只携带槽位与音乐时间，不拥有 Vulkan 对象；不得复制后重复完成或跨渲染器使用。
struct VulkanRenderTicket {
    int slot = -1; // -1 为无效/已完成票据；槽号不编码渲染器身份。
    qint64 musicPositionUs = 0; // 本帧输出音乐位置，单位为微秒，不是编码 PTS。

    // 仅表示持有非负槽号；具体槽位及在途状态由渲染器完成时校验。
    bool valid() const { return slot >= 0; }
};

// 独占 Vulkan 实例、设备和三个离屏目标，无需窗口、surface 或交换链。
// 调用方串行使用本对象，可先提交多帧再按导出顺序完成；本接口不提供并发调用同步。
class VulkanOffscreenRenderer final {
public:
    // 默认不分配 GPU 资源，析构时等待设备后释放所有已创建对象。
    VulkanOffscreenRenderer();
    ~VulkanOffscreenRenderer();
    // 建立导出资源并保存配置，清除加载/错误状态；重新初始化会替换旧实现。
    // 所有带 error 的接口要求传入有效指针，异常信息写入该字符串。
    bool initialize(const ExportSceneConfig& config, QString* error);
    // 提交一帧并填充票据；取消或没有空闲槽时返回 false，不生成新票据。
    bool beginRender(qint64 musicPositionUs, VulkanRenderTicket& ticket,
                     const std::atomic_bool* canceled, QString* error);
    // 最多等待 fence 十秒，释放票据后检查取消；成功时复制为 RGBA8888 图像。
    bool completeRender(VulkanRenderTicket& ticket, QImage& frame,
                        const std::atomic_bool* canceled, QString* error);
    // 同步便捷入口，依次执行 beginRender 与 completeRender。
    bool render(qint64 musicPositionUs, QImage& frame, const std::atomic_bool* canceled, QString* error);
    // 返回统计快照，未初始化时返回默认值。
    VulkanRenderDiagnostics diagnostics() const;
private:
    class Impl;
    std::unique_ptr<Impl> m_impl; // 隔离 Vulkan 资源细节，并负责异常初始化后的清理。
};
} // namespace midi_play::presentation::visualization
