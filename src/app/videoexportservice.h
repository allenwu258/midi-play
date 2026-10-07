#pragma once
#include "domain/music/musicdocument.h"
#include "presentation/visualization/offscreen/vulkanoffscreenrenderer.h"
#include <atomic>
#include <functional>

namespace midi_play::app {
struct VideoExportOptions {
    QString ffmpegExecutable;
    QString outputPath;
    QString sourcePath;
    QString backgroundPath;
    QString soundFontPath;
    presentation::visualization::ExportSceneConfig scene;
    int fps = 60;
    int ratePercent = 100;
    int crf = 20;
    int tailMilliseconds = 500;
    bool includeAudio = true;
    bool includeMetronome = false;
};
enum class VideoExportStatus { Success, Failed, Canceled };
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
struct VideoExportResult {
    VideoExportStatus status = VideoExportStatus::Failed;
    QString error;
    qint64 frames = 0;
    quint64 clippedSamples = 0;
    VideoExportMetrics metrics;
};
class VideoExportService final {
public:
    using Progress = std::function<void(int percent, const QString& phase)>;
    static VideoExportResult exportDocument(std::shared_ptr<const music::MusicDocument> document,
        const VideoExportOptions& options, const std::atomic_bool* canceled = nullptr, Progress progress = {});
};
} // namespace midi_play::app
