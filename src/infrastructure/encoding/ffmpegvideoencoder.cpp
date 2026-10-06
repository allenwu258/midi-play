#include "ffmpegvideoencoder.h"
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <algorithm>
#include <cmath>
#include <limits>

namespace {

QString siblingFfprobe(const QString& ffmpeg)
{
    const QFileInfo executable(ffmpeg);
#if defined(Q_OS_WIN)
    const QString sibling = executable.dir().filePath(QStringLiteral("ffprobe.exe"));
#else
    const QString sibling = executable.dir().filePath(QStringLiteral("ffprobe"));
#endif
    if (QFileInfo(sibling).isFile() && QFileInfo(sibling).isExecutable()) return sibling;
    return QStandardPaths::findExecutable(QStringLiteral("ffprobe"));
}

bool readTimestamp(const QJsonObject& stream, const QString& key, double* value)
{
    if (!value) return false;
    bool ok = false;
    const double parsed = stream.value(key).toString().toDouble(&ok);
    if (!ok || !std::isfinite(parsed)) return false;
    *value = parsed;
    return true;
}

bool validateMetadata(const QString& ffmpeg, const QString& path, QSize expectedSize,
                      int expectedFps, qint64 expectedFrames, bool expectAudio,
                      qint64 expectedDurationUs, int expectedAudioSampleRate,
                      const std::atomic_bool* canceled, QString* error)
{
    if (expectedSize.isEmpty() && expectedFps <= 0 && expectedFrames < 0 && !expectAudio
        && expectedDurationUs <= 0) return true;
    const QString ffprobe = siblingFfprobe(ffmpeg);
    if (ffprobe.isEmpty()) return true;
    QProcess process;
    process.start(ffprobe, {QStringLiteral("-v"), QStringLiteral("error"),
        QStringLiteral("-show_streams"), QStringLiteral("-of"), QStringLiteral("json"), path});
    if (!process.waitForStarted(3000)) {
        *error = QStringLiteral("无法启动 ffprobe 完整性检查：%1").arg(process.errorString());
        return false;
    }
    QElapsedTimer timer;
    timer.start();
    while (!process.waitForFinished(100)) {
        if (canceled && canceled->load()) { process.kill(); process.waitForFinished(3000); return false; }
        if (timer.elapsed() > 15000) {
            process.kill(); process.waitForFinished(3000);
            *error = QStringLiteral("ffprobe 完整性检查超时");
            return false;
        }
    }
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        *error = QStringLiteral("ffprobe 完整性检查失败：%1")
                     .arg(QString::fromUtf8(process.readAllStandardError()).trimmed());
        return false;
    }
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(process.readAllStandardOutput(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()
        || !document.object().value(QStringLiteral("streams")).isArray()) {
        *error = QStringLiteral("ffprobe 返回的流信息无效");
        return false;
    }
    const auto streams = document.object().value(QStringLiteral("streams")).toArray();
    QJsonObject video;
    QJsonObject audio;
    for (const auto& value : streams) {
        const auto stream = value.toObject();
        if (stream.value(QStringLiteral("codec_type")).toString() == QStringLiteral("video") && video.isEmpty()) video = stream;
        if (stream.value(QStringLiteral("codec_type")).toString() == QStringLiteral("audio") && audio.isEmpty()) audio = stream;
    }
    if (video.isEmpty()) { *error = QStringLiteral("视频完整性检查失败：缺少视频流"); return false; }
    if (!expectedSize.isEmpty()
        && (video.value(QStringLiteral("width")).toInt() != expectedSize.width()
            || video.value(QStringLiteral("height")).toInt() != expectedSize.height())) {
        *error = QStringLiteral("视频完整性检查失败：输出尺寸与请求不一致"); return false;
    }
    if (expectedFps > 0) {
        const auto rate = video.value(QStringLiteral("avg_frame_rate")).toString().split(QLatin1Char('/'));
        if (rate.size() != 2 || rate[1].toLongLong() <= 0
            || rate[0].toLongLong() != qint64(expectedFps) * rate[1].toLongLong()) {
            *error = QStringLiteral("视频完整性检查失败：输出帧率与请求不一致"); return false;
        }
    }
    if (expectedFrames >= 0) {
        bool ok = false;
        const qint64 frames = video.value(QStringLiteral("nb_frames")).toString().toLongLong(&ok);
        if (!ok || frames != expectedFrames) {
            *error = QStringLiteral("视频完整性检查失败：输出帧数与请求不一致"); return false;
        }
    }
    if (expectAudio != (!audio.isEmpty())) {
        *error = QStringLiteral("视频完整性检查失败：音频流数量与请求不一致");
        return false;
    }
    // start_time is presentation time. Negative AAC priming PTS and H.264
    // reorder DTS are valid packets; MP4 edit lists compensate for them.
    for (const auto& stream : {video, audio}) {
        if (stream.isEmpty()) continue;
        const bool isAudio = stream.value(QStringLiteral("codec_type")).toString() == QStringLiteral("audio");
        double start = 0.0;
        if (!readTimestamp(stream, QStringLiteral("start_time"), &start) || std::abs(start) > 0.0001) {
            *error = QStringLiteral("视频完整性检查失败：%1呈现起点不是 0")
                         .arg(isAudio ? QStringLiteral("音频") : QStringLiteral("视频"));
            return false;
        }
        if (expectedDurationUs <= 0) continue;
        double duration = 0.0;
        // AAC can expose one final padded packet in some FFmpeg builds.
        const double tolerance = isAudio ? 1024.0 / expectedAudioSampleRate : 0.0001;
        if (!readTimestamp(stream, QStringLiteral("duration"), &duration)
            || std::abs(duration - expectedDurationUs / 1'000'000.0) > tolerance) {
            *error = QStringLiteral("视频完整性检查失败：%1时长与时间线不一致")
                         .arg(isAudio ? QStringLiteral("音频") : QStringLiteral("视频"));
            return false;
        }
    }
    if (expectAudio && (audio.isEmpty() || audio.value(QStringLiteral("codec_name")).toString() != QStringLiteral("aac")
        || audio.value(QStringLiteral("sample_rate")).toString().toInt() != expectedAudioSampleRate
        || audio.value(QStringLiteral("channels")).toInt() != 2)) {
        *error = QStringLiteral("视频完整性检查失败：音频流规格与请求不一致"); return false;
    }
    return true;
}

} // namespace

namespace midi_play::encoding {
FfmpegVideoEncoder::~FfmpegVideoEncoder()
{
    if (m_process.state() != QProcess::NotRunning) {
        m_process.kill();
        m_process.waitForFinished(3000);
    }
}
void FfmpegVideoEncoder::collectDiagnostics()
{
    m_diagnostics += m_process.readAllStandardError();
    if (m_diagnostics.size() > 16384) m_diagnostics = m_diagnostics.right(16384);
    m_process.readAllStandardOutput();
}
bool FfmpegVideoEncoder::processError(QString* error)
{
    collectDiagnostics();
    *error = QStringLiteral("FFmpeg 编码失败：%1").arg(m_diagnostics.isEmpty()
        ? m_process.errorString() : QString::fromUtf8(m_diagnostics).trimmed());
    return false;
}
bool FfmpegVideoEncoder::open(const QString& executable, const QString& output, const QString& pcmPath,
    QSize size, int fps, int crf, qint64 frameCount, int audioSampleRate,
    const std::atomic_bool* canceled, QString* error)
{
    if (m_process.state() != QProcess::NotRunning || size.isEmpty() || size.width() % 2
        || size.height() % 2 || (fps != 30 && fps != 60) || frameCount <= 0
        || (audioSampleRate != 44'100 && audioSampleRate != 48'000)
        || audioSampleRate % fps != 0) {
        *error = QStringLiteral("视频编码参数无效");
        return false;
    }
    if (frameCount > std::numeric_limits<qint64>::max() / ((audioSampleRate / fps) * 8)) {
        *error = QStringLiteral("视频编码时长超过限制");
        return false;
    }
    if (canceled && canceled->load()) return false;
    if (!pcmPath.isEmpty()) {
        const QFileInfo pcm(pcmPath);
        const qint64 expectedBytes = frameCount * (audioSampleRate / fps) * 2 * sizeof(float);
        if (!pcm.isFile() || !pcm.isReadable() || pcm.size() != expectedBytes) {
            *error = QStringLiteral("PCM 音频长度与视频帧数不一致");
            return false;
        }
    }
    m_size = size;
    m_canceled = canceled;
    m_expectedFrames = frameCount;
    m_writtenFrames = 0;
    m_audioSampleRate = audioSampleRate;
    m_diagnostics.clear();
    QStringList args {QStringLiteral("-nostdin"), QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"), QStringLiteral("warning"),
        QStringLiteral("-f"), QStringLiteral("rawvideo"), QStringLiteral("-pixel_format"), QStringLiteral("rgba"),
        QStringLiteral("-video_size"), QStringLiteral("%1x%2").arg(size.width()).arg(size.height()),
        QStringLiteral("-framerate"), QString::number(fps), QStringLiteral("-i"), QStringLiteral("pipe:0")};
    if (!pcmPath.isEmpty()) args << QStringLiteral("-f") << QStringLiteral("f32le") << QStringLiteral("-ar")
        << QString::number(audioSampleRate) << QStringLiteral("-ac") << QStringLiteral("2") << QStringLiteral("-i") << pcmPath;
    args << QStringLiteral("-map") << QStringLiteral("0:v:0") << QStringLiteral("-c:v") << QStringLiteral("libx264")
        << QStringLiteral("-preset") << QStringLiteral("medium") << QStringLiteral("-crf") << QString::number(crf)
        // Keep the video decode timeline non-negative.  B-frame reordering
        // introduces negative DTS and makes FFmpeg add a second MP4 edit list
        // (media_time=1024) to the video track.  That combination is valid,
        // but several FFmpeg/LAV based players apply the video and AAC edit
        // lists inconsistently and repeatedly correct the audio clock.  The
        // export path favors deterministic playback compatibility over the
        // small bitrate saving from B-frames.
        << QStringLiteral("-bf") << QStringLiteral("0")
        << QStringLiteral("-vf") << QStringLiteral("scale=in_range=full:out_range=tv:out_color_matrix=bt709,format=yuv420p")
        << QStringLiteral("-color_range") << QStringLiteral("tv") << QStringLiteral("-colorspace") << QStringLiteral("bt709")
        << QStringLiteral("-color_primaries") << QStringLiteral("bt709") << QStringLiteral("-color_trc") << QStringLiteral("iec61966-2-1")
        << QStringLiteral("-g") << QString::number(fps * 2)
        // The input pipe is CFR. Keep the output policy explicit so a
        // different FFmpeg default cannot duplicate or drop frames.
        << QStringLiteral("-fps_mode") << QStringLiteral("cfr");
    if (!pcmPath.isEmpty()) args << QStringLiteral("-map") << QStringLiteral("1:a:0") << QStringLiteral("-c:a")
        << QStringLiteral("aac") << QStringLiteral("-b:a") << QStringLiteral("192k");
    // Both raw inputs start at zero and have exactly matching durations.
    // Drain each input through EOF without a separate stop condition.  Keep
    // AAC encoder delay in packet timestamps so the MP4 edit list can remove
    // the priming samples without changing the musical start time.
    args << QStringLiteral("-avoid_negative_ts") << QStringLiteral("disabled")
        << QStringLiteral("-use_editlist") << QStringLiteral("1")
        << QStringLiteral("-movflags") << QStringLiteral("+faststart") << QStringLiteral("-f") << QStringLiteral("mp4") << output;
    m_process.start(executable, args);
    if (!m_process.waitForStarted(3000)) return processError(error);
    return true;
}
bool FfmpegVideoEncoder::writeFrame(const QImage& frame, QString* error)
{
    if (m_writtenFrames >= m_expectedFrames) {
        *error = QStringLiteral("视频帧数超过时间线长度");
        return false;
    }
    if (frame.size() != m_size || frame.format() != QImage::Format_RGBA8888
        || frame.bytesPerLine() != m_size.width() * 4) {
        *error = QStringLiteral("视频帧格式无效"); return false;
    }
    const char* data = reinterpret_cast<const char*>(frame.constBits());
    qint64 remaining = frame.sizeInBytes();
    QElapsedTimer stalled; stalled.start();
    while (remaining || m_process.bytesToWrite()) {
        if (m_canceled && m_canceled->load()) return false;
        collectDiagnostics();
        if (m_process.state() != QProcess::Running) return processError(error);
        // Limit Qt's internal pending buffer as well as the application's frame pool.
        if (remaining && m_process.bytesToWrite() < 256 * 1024) {
            const qint64 count = m_process.write(data, std::min<qint64>(remaining, 64 * 1024));
            if (count < 0) return processError(error);
            data += count; remaining -= count;
        }
        if (m_process.waitForBytesWritten(50)) stalled.restart();
        if (stalled.elapsed() > 30000) { *error = QStringLiteral("FFmpeg 输入管道超时"); return false; }
    }
    ++m_writtenFrames;
    return true;
}
bool FfmpegVideoEncoder::finish(QString* error)
{
    if (m_writtenFrames != m_expectedFrames) {
        *error = QStringLiteral("视频帧数不足，无法完成封装");
        return false;
    }
    m_process.closeWriteChannel();
    QElapsedTimer timer; timer.start();
    while (m_process.state() != QProcess::NotRunning) {
        if (m_canceled && m_canceled->load()) return false;
        m_process.waitForFinished(100); collectDiagnostics();
        if (timer.elapsed() > 120000) { *error = QStringLiteral("FFmpeg 封装超时"); return false; }
    }
    if (m_process.exitStatus() != QProcess::NormalExit || m_process.exitCode() != 0) return processError(error);
    return true;
}
bool FfmpegVideoEncoder::validate(const QString& executable, const QString& path,
    const std::atomic_bool* canceled, QString* error, QSize expectedSize, int expectedFps,
    qint64 expectedFrames, bool expectAudio, qint64 expectedDurationUs,
    int expectedAudioSampleRate)
{
    if (canceled && canceled->load()) return false;
    if (!validateMetadata(executable, path, expectedSize, expectedFps, expectedFrames,
                          expectAudio, expectedDurationUs, expectedAudioSampleRate,
                          canceled, error)) return false;
    QProcess process;
    process.start(executable, {QStringLiteral("-v"), QStringLiteral("error"), QStringLiteral("-xerror"),
        QStringLiteral("-nostdin"), QStringLiteral("-nostats"), QStringLiteral("-progress"), QStringLiteral("pipe:1"),
        QStringLiteral("-i"), path, QStringLiteral("-map"), QStringLiteral("0:v:0"),
        QStringLiteral("-map"), QStringLiteral("0:a?"), QStringLiteral("-f"), QStringLiteral("null"), QStringLiteral("-")});
    if (!process.waitForStarted(3000)) { *error = process.errorString(); return false; }
    QByteArray diagnostics;
    QElapsedTimer stalled;
    stalled.start();
    while (!process.waitForFinished(100)) {
        diagnostics += process.readAllStandardError(); diagnostics = diagnostics.right(16384);
        if (!process.readAllStandardOutput().isEmpty()) stalled.restart();
        if (canceled && canceled->load()) { process.kill(); process.waitForFinished(3000); return false; }
        if (stalled.elapsed() > 30000) {
            process.kill(); process.waitForFinished(3000);
            *error = QStringLiteral("视频完整性检查解码超时");
            return false;
        }
    }
    diagnostics += process.readAllStandardError();
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0 || QFileInfo(path).size() == 0) {
        *error = QStringLiteral("视频完整性检查失败：%1").arg(QString::fromUtf8(diagnostics)); return false;
    }
    return true;
}
} // namespace midi_play::encoding
