// 音频导出实现：校验资源/容量上限，复用离线合成并完成事务性编码输出。
#include "audioexportservice.h"
#include "offlineaudiorenderer.h"

#include "domain/playback/metronometimeline.h"
#include "domain/playback/playbackmodel.h"
#include "infrastructure/audio/fluidsynthengine.h"

#include <QFileInfo>

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace midi_play::app {
namespace {

// 以长双精度计算后舍入到最近样本帧，避免整秒之外的截断偏移。
qint64 toFrame(qint64 timeUs, int sampleRate)
{
    return static_cast<qint64>(std::llround(static_cast<long double>(timeUs) * sampleRate / 1'000'000));
}

// 构造统一失败结果，不把尚未完成的文件视为成功输出。
AudioExportResult failure(const QString& error)
{
    return {AudioExportStatus::Failed, error, 0};
}

} // namespace

// 同步音频导出事务：参数校验后合成 PCM，编码器 finish 成功才发布目标文件。
// 文档由共享只读指针保持存活；进度回调在当前调用线程执行。
AudioExportResult AudioExportService::exportDocument(
    std::shared_ptr<const music::MusicDocument> document,
    const AudioExportOptions& options, const std::atomic_bool* canceled, Progress progress)
{
    if (!document || !document->isValid()) return failure(QStringLiteral("没有可导出的乐曲"));
    if (options.sampleRate != 44100 && options.sampleRate != 48000)
        return failure(QStringLiteral("采样率必须为 44100 或 48000 Hz"));
    if (options.format == encoding::AudioFileFormat::Mp3
        && options.bitrateKbps != 128 && options.bitrateKbps != 160
        && options.bitrateKbps != 192 && options.bitrateKbps != 256
        && options.bitrateKbps != 320)
        return failure(QStringLiteral("不支持的 MP3 码率"));
    if (options.tailMilliseconds < 0 || options.tailMilliseconds > 5000)
        return failure(QStringLiteral("尾音长度必须在 0 到 5000 毫秒之间"));
    const QFileInfo font(options.soundFontPath);
    if (!font.isFile() || !font.isReadable())
        return failure(QStringLiteral("无法读取 SoundFont: %1").arg(options.soundFontPath));
    if (options.outputPath.trimmed().isEmpty()) return failure(QStringLiteral("未指定输出路径"));
    if (QFileInfo(options.outputPath).absoluteFilePath() == font.absoluteFilePath())
        return failure(QStringLiteral("输出路径不能覆盖 SoundFont"));
    if (canceled && canceled->load()) return {AudioExportStatus::Canceled, {}, 0};

    playback::PlaybackModel model(document);
    const auto& events = model.globalEvents();
    const qint64 lastEventUs = events.isEmpty() ? 0 : events.constLast().timestampUs;
    const qint64 durationUs = std::max(model.durationUs(), lastEventUs);
    constexpr qint64 kMaxExportUs = 24LL * 60 * 60 * 1'000'000;
    if (durationUs < 0 || durationUs > kMaxExportUs)
        return failure(QStringLiteral("乐曲时长超过导出限制（24 小时）"));
    const qint64 totalFrames = toFrame(durationUs + options.tailMilliseconds * 1000LL,
                                       options.sampleRate);
    const qint64 musicalEndFrame = toFrame(durationUs, options.sampleRate);
    if (totalFrames <= 0) return failure(QStringLiteral("乐曲没有可导出的时长"));
    // 立体声 16 位 PCM 每帧四字节，标准 RIFF 的 32 位长度字段限制总文件大小。
    if (options.format == encoding::AudioFileFormat::Wav
        && static_cast<quint64>(totalFrames) * 4 > std::numeric_limits<quint32>::max() - 36)
        return failure(QStringLiteral("WAV 超过 4 GB RIFF 格式上限，请使用 MP3"));

    QString error;
    auto encoder = encoding::createAudioFileEncoder(options.format);
    if (!encoder->open(options.outputPath, options.sampleRate, options.bitrateKbps, &error))
        return failure(error);

    // 独立音频导出固定 100% 倍率，与可变速的视频导出参数区分。
    const OfflineAudioOptions renderOptions {font.absoluteFilePath(), options.sampleRate, 100,
                                            options.includeMetronome, totalFrames};
    const auto result = OfflineAudioRenderer::render(document, renderOptions,
        [&encoder](const float* left, const float* right, int count, QString* error) {
            return encoder->write(left, right, count, error);
        }, canceled, std::move(progress));
    if (result.status != AudioExportStatus::Success) return result;
    if (canceled && canceled->load()) return {AudioExportStatus::Canceled, {}, result.frames};
    // finish 写出尾块/文件头并提交目标文件，此前失败或取消均不发布成品。
    if (!encoder->finish(&error)) return failure(error);
    return result;
}

} // namespace midi_play::app
