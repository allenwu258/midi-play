#include "videoexportservice.h"
#include "exporttimeline.h"
#include "offlineaudiorenderer.h"
#include "domain/playback/playbackmodel.h"
#include "domain/visualization/playbackvisualizationprojector.h"
#include "infrastructure/encoding/ffmpegprobe.h"
#include "infrastructure/encoding/ffmpegvideoencoder.h"
#include "infrastructure/encoding/ffmpegvideoencoderworker.h"
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QStorageInfo>
#include <QTemporaryDir>
#include <algorithm>
#include <deque>
#include <vector>

namespace midi_play::app {
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
    const QFileInfo output(options.outputPath);
    if (options.outputPath.trimmed().isEmpty() || output.suffix().compare(QStringLiteral("mp4"), Qt::CaseInsensitive)
        || !output.dir().exists() || output.isDir()) return failure(QStringLiteral("请选择有效的 MP4 输出路径"));
    const auto identity = [](const QString& path) {
        const QFileInfo info(path); const QString canonical = info.canonicalFilePath();
        return canonical.isEmpty() ? info.absoluteFilePath() : canonical;
    };
    for (const auto& input : {options.sourcePath, options.soundFontPath, options.backgroundPath, options.ffmpegExecutable}) {
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
    // Project from this job's document, never from a potentially changed GUI chart.
    scene.state.chart = visualization::PlaybackVisualizationProjector().project(*document, 1, {});
    if (!scene.state.chart) return failure(QStringLiteral("无法构建视频场景"));
    scene.state.durationUs = end;
    presentation::visualization::VulkanOffscreenRenderer renderer;
    QString error;
    if (!renderer.initialize(scene, &error)) return failure(error);
    QTemporaryDir directory(output.dir().filePath(QStringLiteral(".midi-play-video-XXXXXX")));
    if (!directory.isValid()) return failure(QStringLiteral("无法创建导出临时目录"));
    QString pcmPath;
    quint64 clipped = 0;
    if (options.includeAudio) {
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
        // Windows may deny a second reader while the writer handle is open.
        // Close the PCM file before FFmpeg starts consuming it.
        pcm.close();
        if (pcm.error() != QFile::NoError) return failure(pcm.errorString());
        if (result.frames != timeline.sampleCount() || QFileInfo(pcmPath).size() != timeline.sampleCount() * 8)
            return failure(QStringLiteral("离线音频长度与视频时间线不一致"));
        clipped = result.clippedSamples;
    }
    const QString temporaryOutput = directory.filePath(QStringLiteral("video.mp4"));
    encoding::FfmpegVideoEncoderWorker encoder(3);
    if (!encoder.open(ffmpeg.executablePath, temporaryOutput, pcmPath, size, options.fps, options.crf,
                      timeline.frameCount(), ExportTimeline::audioSampleRate, canceled, &error))
        return failure(error);

    struct PendingFrame {
        qint64 index = 0;
        presentation::visualization::VulkanRenderTicket ticket;
    };
    std::deque<PendingFrame> pending;
    QElapsedTimer videoTimer;
    videoTimer.start();
    auto completeNext = [&]() {
        if (pending.empty()) return true;
        auto current = std::move(pending.front());
        pending.pop_front();
        QImage frame;
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
            --n;
            continue;
        }
        pending.push_back({n, ticket});
        ++metrics.submittedFrames;
        metrics.peakInFlightFrames = std::max(metrics.peakInFlightFrames, int(pending.size()));
    }
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
    if (!encoding::FfmpegVideoEncoder::validate(ffmpeg.executablePath, temporaryOutput, canceled, &error,
                                                size, options.fps, timeline.frameCount(), options.includeAudio,
                                                timeline.videoDurationUs(), ExportTimeline::audioSampleRate))
        return failure(error);
    metrics.validationMs = validationTimer.elapsed();
    if (isCanceled()) return failure({});
    QFile source(temporaryOutput);
    QElapsedTimer commitTimer;
    commitTimer.start();
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
    report(100, QStringLiteral("视频已导出"));
    return {VideoExportStatus::Success, {}, timeline.frameCount(), clipped, metrics};
#endif
}
} // namespace midi_play::app
