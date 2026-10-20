// 视频导出编排实现：先合成 PCM，再流水渲染/编码，校验后原子提交 MP4。
#include "videoexportservice.h"
#include "exporttimeline.h"
#include "offlineaudiorenderer.h"
#include "presentation/visualization/exportframing.h"
#include "domain/playback/playbackmodel.h"
#include "domain/visualization/playbackvisualizationprojector.h"
#include "infrastructure/encoding/ffmpegprobe.h"
#include "infrastructure/encoding/backgroundaudioexport.h"
#include "infrastructure/media/videoframereader.h"
#include "infrastructure/encoding/ffmpegvideoencoder.h"
#include "infrastructure/encoding/ffmpegvideoencoderworker.h"
#include <QDir>
#include <QDateTime>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStorageInfo>
#include <QTemporaryDir>
#include <algorithm>
#include <deque>
#include <vector>

namespace midi_play::app {
namespace {
// 累计纳秒舍入到最近毫秒，供任务结果展示。
qint64 nanosecondsToMilliseconds(quint64 nanoseconds)
{
    return qint64((nanoseconds + 500'000) / 1'000'000);
}

// 显式启用诊断并配置路径时追加一条 JSONL；诊断写入失败不影响导出结果。
void appendVideoDiagnostics(const VideoExportOptions& options, qint64 frameCount,
                            const VideoExportMetrics& metrics,
                            const presentation::visualization::VulkanRenderDiagnostics& renderer,
                            const encoding::FfmpegVideoEncoderDiagnostics& encoder)
{
    if (!renderer.enabled) return;
    const QString path = qEnvironmentVariable("MIDI_PLAY_VIDEO_DIAGNOSTICS_PATH").trimmed();
    if (path.isEmpty()) return;
    QJsonObject json;
    json.insert(QStringLiteral("schemaVersion"), 1);
    json.insert(QStringLiteral("timestampUtc"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    json.insert(QStringLiteral("frameCount"), frameCount);
    json.insert(QStringLiteral("fps"), options.fps);
    json.insert(QStringLiteral("width"), options.scene.outputSize.width());
    json.insert(QStringLiteral("height"), options.scene.outputSize.height());
    json.insert(QStringLiteral("retainMeasureGutter"), options.scene.retainMeasureGutter);
    json.insert(QStringLiteral("logicalSceneWidth"), renderer.logicalSceneSize.width());
    json.insert(QStringLiteral("logicalSceneHeight"), renderer.logicalSceneSize.height());
    json.insert(QStringLiteral("cropLeft"), renderer.logicalCropRect.left());
    json.insert(QStringLiteral("cropWidth"), renderer.logicalCropRect.width());
    json.insert(QStringLiteral("cropHeight"), renderer.logicalCropRect.height());
    json.insert(QStringLiteral("geometryScale"), renderer.geometryScale);
    json.insert(QStringLiteral("includeAudio"), options.includeAudio);
    json.insert(QStringLiteral("vulkanDeviceName"), renderer.deviceName);
    json.insert(QStringLiteral("vulkanDeviceType"), renderer.deviceType);
    json.insert(QStringLiteral("vulkanVendorId"), int(renderer.vendorId));
    json.insert(QStringLiteral("vulkanDriverVersion"), qint64(renderer.driverVersion));
    json.insert(QStringLiteral("vulkanQueueFamily"), int(renderer.queueFamily));
    json.insert(QStringLiteral("vulkanTimestampValidBits"), int(renderer.timestampValidBits));
    json.insert(QStringLiteral("vulkanDeviceLocalHeapBytes"), qint64(renderer.deviceLocalHeapBytes));
    json.insert(QStringLiteral("vulkanHostVisibleHeapBytes"), qint64(renderer.hostVisibleHeapBytes));
    json.insert(QStringLiteral("readbackHostCached"), renderer.readbackHostCached);
    json.insert(QStringLiteral("gpuTimestamps"), renderer.gpuTimestamps);
    json.insert(QStringLiteral("submittedFrames"), renderer.submittedFrames);
    json.insert(QStringLiteral("completedFrames"), renderer.completedFrames);
    json.insert(QStringLiteral("encodedFrames"), encoder.encodedFrames);
    json.insert(QStringLiteral("peakInFlightFrames"), metrics.peakInFlightFrames);
    json.insert(QStringLiteral("peakEncoderQueueDepth"), encoder.peakQueueDepth);
    json.insert(QStringLiteral("scenePrepareNs"), qint64(renderer.scenePrepareNs));
    json.insert(QStringLiteral("bufferUploadNs"), qint64(renderer.bufferUploadNs));
    json.insert(QStringLiteral("commandRecordNs"), qint64(renderer.commandRecordNs));
    json.insert(QStringLiteral("queueSubmitNs"), qint64(renderer.queueSubmitNs));
    json.insert(QStringLiteral("fenceWaitNs"), qint64(renderer.fenceWaitNs));
    json.insert(QStringLiteral("readbackNs"), qint64(renderer.readbackNs));
    json.insert(QStringLiteral("qimageCopyNs"), qint64(renderer.qimageCopyNs));
    json.insert(QStringLiteral("gpuFrameNs"), qint64(renderer.gpuFrameNs));
    json.insert(QStringLiteral("encoderFrameBufferWaitNs"), qint64(encoder.recycledFrameWaitNs));
    json.insert(QStringLiteral("encoderQueueWaitNs"), qint64(encoder.queueWaitNs));
    json.insert(QStringLiteral("ffmpegWriteNs"), qint64(encoder.writeNs));
    json.insert(QStringLiteral("videoEncoder"), encoder.videoEncoder);
    json.insert(QStringLiteral("hardwareAccelerated"), encoder.hardwareAccelerated);
    json.insert(QStringLiteral("encoderSelectionNs"), qint64(encoder.encoderSelectionNs));
    json.insert(QStringLiteral("audioRenderMs"), metrics.audioRenderMs);
    json.insert(QStringLiteral("videoPipelineMs"), metrics.videoPipelineMs);
    json.insert(QStringLiteral("muxFinalizeMs"), metrics.muxFinalizeMs);
    json.insert(QStringLiteral("validationMs"), metrics.validationMs);
    json.insert(QStringLiteral("commitMs"), metrics.commitMs);
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) return;
    file.write(QJsonDocument(json).toJson(QJsonDocument::Compact));
    file.write("\n");
}
} // namespace

// 工作线程同步导出入口：校验资源，生成 PCM，流水渲染/编码，验证成品后提交目标文件。
// progress 在调用线程执行；取消只在各阶段检查点生效，调用方不得直接更新 GUI。
VideoExportResult VideoExportService::exportDocument(std::shared_ptr<const music::MusicDocument> document,
    const VideoExportOptions& options, const std::atomic_bool* canceled, Progress progress)
{
    const auto isCanceled = [canceled] { return canceled && canceled->load(); };
    VideoExportMetrics metrics;
    const auto failure = [&isCanceled, &metrics](const QString& error) {
        return VideoExportResult{isCanceled() ? VideoExportStatus::Canceled : VideoExportStatus::Failed, error,
                                 0, 0, metrics};
    };
    const auto report = [&progress](int value, const QString& phase) { if (progress) progress(value, phase); };
#if !MIDI_PLAY_HAS_VULKAN
    Q_UNUSED(document) Q_UNUSED(options) Q_UNUSED(report)
    return failure(QStringLiteral("此版本不支持 Vulkan 视频导出"));
#else
    if (isCanceled()) return failure({});
    if (!document || !document->isValid()) return failure(QStringLiteral("没有可导出的乐曲"));
    const QSize size = options.scene.outputSize;
    if (size.width() < 320 || size.height() < 240 || size.width() > 3840 || size.height() > 2160
        || size.width() % 2 || size.height() % 2 || options.scene.logicalSize.width() < 320
        || options.scene.logicalSize.height() < 240 || options.scene.logicalSize.width() > 7680
        || options.scene.logicalSize.height() > 4320 || options.crf < 17 || options.crf > 23)
        return failure(QStringLiteral("视频尺寸、构图或质量参数无效"));
    if (!presentation::visualization::isWideExportFrame(size)
        || !presentation::visualization::isWideExportFrame(options.scene.logicalSize))
        return failure(QStringLiteral("视频导出目前仅支持 16:9 画幅，请选择 720p、1080p 或 4K 分辨率"));
    const QFileInfo output(options.outputPath);
    if (options.outputPath.trimmed().isEmpty() || output.suffix().compare(QStringLiteral("mp4"), Qt::CaseInsensitive)
        || !output.dir().exists() || output.isDir()) return failure(QStringLiteral("请选择有效的 MP4 输出路径"));
    // 已存在文件使用规范路径解析链接；不存在的输出使用绝对路径参与覆盖保护。
    const auto identity = [](const QString& path) {
        const QFileInfo info(path); const QString canonical = info.canonicalFilePath();
        return canonical.isEmpty() ? info.absoluteFilePath() : canonical;
    };
    for (const auto& input : {options.sourcePath, options.soundFontPath, options.backgroundPath,
                              options.scene.backgroundVideoPath, options.ffmpegExecutable}) {
        if (!input.isEmpty() && identity(input).compare(identity(options.outputPath), Qt::CaseInsensitive) == 0)
            return failure(QStringLiteral("输出路径不能覆盖输入资源"));
    }
    report(0, QStringLiteral("正在准备导出"));
    const auto ffmpeg = encoding::probeFfmpeg(options.ffmpegExecutable, false);
    if (!ffmpeg.valid) return failure(ffmpeg.error);
    playback::PlaybackModel model(document);
    const qint64 end = std::max(model.durationUs(), model.globalEvents().isEmpty() ? 0 : model.globalEvents().constLast().timestampUs);
    const ExportTimeline timeline(end, options.fps, options.ratePercent, options.tailMilliseconds);
    if (!timeline.valid()) return failure(QStringLiteral("乐曲时长、帧率、速度或尾音参数无效"));
    auto scene = options.scene;
    // 从任务持有的文档独立投影，避免导出期间 GUI 切换乐曲影响场景。
    scene.state.chart = visualization::PlaybackVisualizationProjector().project(*document, 1, {});
    if (!scene.state.chart) return failure(QStringLiteral("无法构建视频场景"));
    scene.state.durationUs = end;
    presentation::visualization::VulkanOffscreenRenderer renderer;
    QString error;
    if (!renderer.initialize(scene, &error, canceled)) return failure(error);
    QTemporaryDir directory(output.dir().filePath(QStringLiteral(".midi-play-video-XXXXXX")));
    if (!directory.isValid()) return failure(QStringLiteral("无法创建导出临时目录"));
    QString pcmPath;
    quint64 clipped = 0;
    if (options.includeAudio) {
        // PCM 为交错双声道 float，每帧 8 字节；预留额外空间供临时视频封装使用。
        const QFileInfo font(options.soundFontPath);
        if (!font.isReadable() || !font.isFile()) return failure(QStringLiteral("请先配置有效的 SoundFont"));
        const QStorageInfo storage(output.dir());
        if (storage.isValid() && storage.bytesAvailable() < timeline.sampleCount() * 8 + 64 * 1024 * 1024)
            return failure(QStringLiteral("磁盘空间不足以生成离线音频"));
        pcmPath = directory.filePath(QStringLiteral("audio.pcm"));
        QFile pcm(pcmPath);
        if (!pcm.open(QIODevice::WriteOnly)) return failure(pcm.errorString());
        std::vector<float> interleaved(8192);
        const OfflineAudioOptions audio {font.absoluteFilePath(), ExportTimeline::audioSampleRate, options.ratePercent,
                                        options.includeMetronome, timeline.sampleCount()};
        QElapsedTimer timer;
        timer.start();
        // 音频覆盖完整视频帧时长；该阶段占总进度的前 20%。
        const auto result = OfflineAudioRenderer::render(document, audio,
            [&pcm, &interleaved](const float* left, const float* right, int count, QString* error) {
                for (int i = 0; i < count; ++i) { interleaved[i * 2] = left[i]; interleaved[i * 2 + 1] = right[i]; }
                const qint64 bytes = qint64(count) * 8;
                if (pcm.write(reinterpret_cast<const char*>(interleaved.data()), bytes) == bytes) return true;
                *error = pcm.errorString(); return false;
            }, canceled, [&report](int value) { report(value / 5, QStringLiteral("正在合成音频")); });
        metrics.audioRenderMs = timer.elapsed();
        if (result.status != AudioExportStatus::Success) return failure(result.error);
        if (!pcm.flush()) return failure(pcm.errorString());
        // Windows 可能拒绝写句柄未关闭时的第二个读者，启动 FFmpeg 前先关闭 PCM。
        pcm.close();
        if (pcm.error() != QFile::NoError) return failure(pcm.errorString());
        if (result.frames != timeline.sampleCount() || QFileInfo(pcmPath).size() != timeline.sampleCount() * 8)
            return failure(QStringLiteral("离线音频长度与视频时间线不一致"));
        clipped = result.clippedSamples;
    }
    if (!options.backgroundVideoMuted && !scene.backgroundVideoPath.isEmpty()) {
        media::VideoFrameReader video;
        if (!video.open(scene.backgroundVideoPath, scene.backgroundMaximumDimension, &error, canceled)) return failure(error);
        if (video.metadata().hasAudio) {
            report(20, QStringLiteral("正在混合视频原声"));
            if (!encoding::mixBackgroundVideoAudio(ffmpeg.executablePath, scene.backgroundVideoPath,
                video.metadata().durationUs, end, options.ratePercent, ExportTimeline::audioSampleRate,
                timeline.sampleCount(), directory.path(), pcmPath, canceled, &error)) return failure(error);
        }
    }
    const QString temporaryOutput = directory.filePath(QStringLiteral("video.mp4"));
    encoding::FfmpegVideoEncoderWorker encoder(3);
    if (!encoder.open(ffmpeg.executablePath, temporaryOutput, pcmPath, size, options.fps, options.crf,
                      timeline.frameCount(), ExportTimeline::audioSampleRate, canceled, &error))
        return failure(error);

    // 队列保存音乐帧顺序与对应 GPU 票据，按提交顺序回读后送入编码队列。
    struct PendingFrame {
        qint64 index = 0;
        presentation::visualization::VulkanRenderTicket ticket;
    };
    std::deque<PendingFrame> pending;
    QElapsedTimer videoTimer;
    videoTimer.start();
    // 先取得编码器回收的 QImage，再完成最早 GPU 帧，减少整帧内存重复分配。
    auto completeNext = [&]() {
        if (pending.empty()) return true;
        auto current = std::move(pending.front());
        pending.pop_front();
        QImage frame;
        if (!encoder.acquireFrame(frame, &error)) return false;
        if (!renderer.completeRender(current.ticket, frame, canceled, &error)) return false;
        if (!encoder.submitFrame(std::move(frame), &error)) return false;
        metrics.encodedFrames = encoder.encodedFrames();
        report(20 + int((current.index + 1) * 65 / timeline.frameCount()),
               QStringLiteral("正在渲染视频 %1 / %2 帧")
                   .arg(current.index + 1).arg(timeline.frameCount()));
        return true;
    };
    for (qint64 n = 0; n < timeline.frameCount(); ++n) {
        if (isCanceled()) return failure({});
        presentation::visualization::VulkanRenderTicket ticket;
        error.clear();
        if (!renderer.beginRender(timeline.musicPositionUs(n), ticket, canceled, &error)) {
            if (!error.isEmpty() || pending.empty()) return failure(error);
            if (!completeNext()) return failure(error);
            // 无空闲 GPU 槽但没有错误时，完成最早一帧后重试当前绝对帧索引。
            --n;
            continue;
        }
        pending.push_back({n, ticket});
        ++metrics.submittedFrames;
        metrics.peakInFlightFrames = std::max(metrics.peakInFlightFrames, int(pending.size()));
    }
    // 所有帧提交后排空 GPU 队列，编码器 finish 再等待编码队列并封装收尾。
    while (!pending.empty()) {
        if (isCanceled() || !completeNext()) return failure(error);
    }
    metrics.videoPipelineMs = videoTimer.elapsed();
    report(85, QStringLiteral("正在封装 MP4"));
    QElapsedTimer muxTimer;
    muxTimer.start();
    const bool encoderFinished = encoder.finish(&error);
    metrics.encodedFrames = encoder.encodedFrames();
    metrics.muxFinalizeMs = muxTimer.elapsed();
    if (!encoderFinished) return failure(error);
    report(90, QStringLiteral("正在检查视频完整性"));
    QElapsedTimer validationTimer;
    validationTimer.start();
    // ffprobe 可用时校验媒体规格，并始终执行完整解码检查，再允许覆盖最终输出。
    if (!encoding::FfmpegVideoEncoder::validate(ffmpeg.executablePath, temporaryOutput, canceled, &error,
                                                size, options.fps, timeline.frameCount(), !pcmPath.isEmpty(),
                                                timeline.videoDurationUs(), ExportTimeline::audioSampleRate))
        return failure(error);
    metrics.validationMs = validationTimer.elapsed();
    if (isCanceled()) return failure({});
    QFile source(temporaryOutput);
    QElapsedTimer commitTimer;
    commitTimer.start();
    // 禁用直接写回退，取消/失败时保留原目标；仅 commit 成功发布新视频。
    QSaveFile destination(output.absoluteFilePath()); destination.setDirectWriteFallback(false);
    if (!source.open(QIODevice::ReadOnly) || !destination.open(QIODevice::WriteOnly))
        return failure(QStringLiteral("无法安全写入输出文件：%1").arg(destination.errorString()));
    const qint64 total = source.size();
    qint64 copied = 0;
    while (!source.atEnd()) {
        if (isCanceled()) return failure({});
        const QByteArray block = source.read(1024 * 1024);
        if (block.isEmpty() || destination.write(block) != block.size()) return failure(QStringLiteral("写入视频失败，磁盘空间可能不足"));
        copied += block.size();
        report(95 + int(copied * 4 / total), QStringLiteral("正在保存视频"));
    }
    if (isCanceled()) return failure({});
    if (!destination.commit()) return failure(destination.errorString());
    metrics.commitMs = commitTimer.elapsed();
    const auto rendererDiagnostics = renderer.diagnostics();
    const auto encoderDiagnostics = encoder.diagnostics();
    if (rendererDiagnostics.enabled) {
        metrics.scenePrepareMs = nanosecondsToMilliseconds(rendererDiagnostics.scenePrepareNs);
        metrics.bufferUploadMs = nanosecondsToMilliseconds(rendererDiagnostics.bufferUploadNs);
        metrics.commandRecordMs = nanosecondsToMilliseconds(rendererDiagnostics.commandRecordNs);
        metrics.queueSubmitMs = nanosecondsToMilliseconds(rendererDiagnostics.queueSubmitNs);
        metrics.fenceWaitMs = nanosecondsToMilliseconds(rendererDiagnostics.fenceWaitNs);
        metrics.readbackMs = nanosecondsToMilliseconds(rendererDiagnostics.readbackNs);
        metrics.qimageCopyMs = nanosecondsToMilliseconds(rendererDiagnostics.qimageCopyNs);
        metrics.gpuFrameMs = nanosecondsToMilliseconds(rendererDiagnostics.gpuFrameNs);
        metrics.encoderFrameBufferWaitMs = nanosecondsToMilliseconds(encoderDiagnostics.recycledFrameWaitNs);
        metrics.encoderQueueWaitMs = nanosecondsToMilliseconds(encoderDiagnostics.queueWaitNs);
        metrics.ffmpegWriteMs = nanosecondsToMilliseconds(encoderDiagnostics.writeNs);
        metrics.peakEncoderQueueDepth = encoderDiagnostics.peakQueueDepth;
        metrics.videoEncoder = encoderDiagnostics.videoEncoder;
        metrics.hardwareAccelerated = encoderDiagnostics.hardwareAccelerated;
        metrics.encoderSelectionMs = nanosecondsToMilliseconds(encoderDiagnostics.encoderSelectionNs);
        metrics.gpuTimestamps = rendererDiagnostics.gpuTimestamps;
        metrics.vulkanDeviceName = rendererDiagnostics.deviceName;
        metrics.vulkanDeviceType = rendererDiagnostics.deviceType;
        appendVideoDiagnostics(options, timeline.frameCount(), metrics, rendererDiagnostics, encoderDiagnostics);
    }
    report(100, QStringLiteral("视频已导出"));
    return {VideoExportStatus::Success, {}, timeline.frameCount(), clipped, metrics};
#endif
}
} // namespace midi_play::app
