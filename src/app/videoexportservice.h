#pragma once
// 视频导出应用接口：离线音频、Vulkan 帧流水、FFmpeg 编码及最终成品提交。
#include "domain/music/musicdocument.h"
#include "presentation/visualization/offscreen/vulkanoffscreenrenderer.h"
#include <atomic>
#include <functional>

namespace midi_play::app {
// 一次任务的输入快照；资源路径用于覆盖保护，scene 提供独立构图/外观参数。
struct VideoExportOptions {
    QString ffmpegExecutable;
    QString outputPath;
    QString sourcePath;
    QString backgroundPath;
    bool backgroundVideoMuted = true;
    QString soundFontPath;
    presentation::visualization::ExportSceneConfig scene;
    int fps = 60;
    int ratePercent = 100;
    int crf = 20; // 17..23 的质量参数，具体硬件编码器由基础设施转换。
    int tailMilliseconds = 500;
    bool includeAudio = true;
    bool includeMetronome = false;
};
// 取消不等同于错误，GUI 可以单独恢复空闲状态。
enum class VideoExportStatus { Success, Failed, Canceled };
// 导出阶段耗时以毫秒计；细粒度渲染/编码信息在诊断开启时填充。
struct VideoExportMetrics {
    qint64 audioRenderMs = 0;
    qint64 videoPipelineMs = 0;
    qint64 muxFinalizeMs = 0;
    qint64 validationMs = 0;
    qint64 commitMs = 0;
    qint64 submittedFrames = 0;
    qint64 encodedFrames = 0;
    int peakInFlightFrames = 0;
    qint64 scenePrepareMs = 0;
    qint64 bufferUploadMs = 0;
    qint64 commandRecordMs = 0;
    qint64 queueSubmitMs = 0;
    qint64 fenceWaitMs = 0;
    qint64 readbackMs = 0;
    qint64 qimageCopyMs = 0;
    qint64 gpuFrameMs = 0;
    qint64 encoderFrameBufferWaitMs = 0;
    qint64 encoderQueueWaitMs = 0;
    qint64 ffmpegWriteMs = 0;
    int peakEncoderQueueDepth = 0;
    QString videoEncoder;
    bool hardwareAccelerated = false;
    qint64 encoderSelectionMs = 0;
    bool gpuTimestamps = false;
    QString vulkanDeviceName;
    QString vulkanDeviceType;
};
// 仅最终媒体校验及目标提交都成功才返回 Success。
struct VideoExportResult {
    VideoExportStatus status = VideoExportStatus::Failed;
    QString error;
    qint64 frames = 0;
    quint64 clippedSamples = 0;
    VideoExportMetrics metrics;
};
// 同步后台任务入口，使用只读文档及配置快照，不访问实时 GUI 图表。
class VideoExportService final {
public:
    // 进度和阶段文本由任务线程发出；GUI 通过原子采样或排队信号接收，不可直接访问控件。
    using Progress = std::function<void(int percent, const QString& phase)>;
    // 支持可选取消；临时资源由局部对象清理，最终目标用 QSaveFile 提交。
    static VideoExportResult exportDocument(std::shared_ptr<const music::MusicDocument> document,
        const VideoExportOptions& options, const std::atomic_bool* canceled = nullptr, Progress progress = {});
};
} // namespace midi_play::app
