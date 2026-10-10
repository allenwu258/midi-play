// FFmpeg 视频进程实现：编码器选择缓存、管道压力控制及媒体完整性校验。
#include "ffmpegvideoencoder.h"
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QHash>
#include <QMutex>
#include <QMutexLocker>
#include <QStandardPaths>
#include <algorithm>
#include <cmath>
#include <limits>

namespace {

// 默认软件编码器，只有实际 NVENC 探测通过才标记硬件加速。
struct VideoEncoderSelection {
    QString name = QStringLiteral("libx264");
    bool hardwareAccelerated = false;
};

QMutex encoderSelectionMutex;
QHash<QString, VideoEncoderSelection> encoderSelectionCache;

// 编码能力与尺寸/帧率/质量/环境覆盖项有关，缓存身份覆盖这些输入。
QString encoderCacheKey(const QString& executable, QSize size, int fps, int crf,
                        const QString& preference)
{
    return QStringLiteral("%1|%2x%3|%4|%5|%6")
        .arg(QFileInfo(executable).absoluteFilePath()).arg(size.width()).arg(size.height())
        .arg(fps).arg(crf).arg(preference);
}

// 探测与正式编码共用参数，保持质量排序、颜色转换和 CFR 约定一致。
QStringList videoEncodingArguments(const VideoEncoderSelection& encoder, int crf, int fps)
{
    QStringList args {QStringLiteral("-c:v"), encoder.name};
    if (encoder.hardwareAccelerated) {
        // CQ 与 CRF 控制方式不同；NVENC 使用质量目标 VBR，保持 GUI 质量档位顺序。
        args << QStringLiteral("-preset") << QStringLiteral("p4")
             << QStringLiteral("-rc") << QStringLiteral("vbr")
             << QStringLiteral("-cq") << QString::number(crf)
             << QStringLiteral("-b:v") << QStringLiteral("0");
    } else {
        args << QStringLiteral("-preset") << QStringLiteral("medium")
             << QStringLiteral("-crf") << QString::number(crf);
    }
    // 关闭 B 帧以减少部分播放器对 MP4 编辑列表的兼容问题。
    // 两种视频编码器都保持零起点/非负解码时间，并共用全范围 RGB 到电视范围 BT.709 的转换。
    args << QStringLiteral("-bf") << QStringLiteral("0")
         << QStringLiteral("-vf") << QStringLiteral("scale=in_range=full:out_range=tv:out_color_matrix=bt709,format=yuv420p")
         << QStringLiteral("-color_range") << QStringLiteral("tv")
         << QStringLiteral("-colorspace") << QStringLiteral("bt709")
         << QStringLiteral("-color_primaries") << QStringLiteral("bt709")
         << QStringLiteral("-color_trc") << QStringLiteral("iec61966-2-1")
         << QStringLiteral("-g") << QString::number(fps * 2)
         << QStringLiteral("-fps_mode") << QStringLiteral("cfr");
    return args;
}

// 以目标尺寸真实编码两帧到空封装，轮询取消，最多约十秒。
bool probeVideoEncoder(const QString& executable, const VideoEncoderSelection& encoder,
                       QSize size, int fps, int crf, const std::atomic_bool* canceled)
{
    if (canceled && canceled->load()) return false;
    QProcess process;
    QStringList args {
        QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"), QStringLiteral("error"),
        QStringLiteral("-nostdin"), QStringLiteral("-f"), QStringLiteral("lavfi"),
        QStringLiteral("-i"), QStringLiteral("color=c=black:s=%1x%2:r=%3,format=rgba")
            .arg(size.width()).arg(size.height()).arg(fps),
        QStringLiteral("-frames:v"), QStringLiteral("2")};
    args += videoEncodingArguments(encoder, crf, fps);
    args << QStringLiteral("-f") << QStringLiteral("null") << QStringLiteral("-");
    process.start(executable, args);
    if (!process.waitForStarted(1500)) return false;
    QElapsedTimer timer;
    timer.start();
    while (!process.waitForFinished(100)) {
        if (canceled && canceled->load()) {
            process.kill();
            process.waitForFinished(1000);
            return false;
        }
        if (timer.elapsed() > 10'000) {
            process.kill();
            process.waitForFinished(1000);
            return false;
        }
    }
    return process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0;
}

// 以实际编码探测选择 NVENC 或软件回退；缓存值是运行时结果，并非仅检查编码器名称存在。
VideoEncoderSelection selectVideoEncoder(const QString& executable, QSize size, int fps, int crf,
                                         const std::atomic_bool* canceled)
{
    // 环境变量便于复现软/硬件比较；空值、auto、h264_nvenc 尝试 NVENC，其余值使用软件路径。
    // 任意编码器名并不共享 NVENC 质量参数，因此不直接透传给 FFmpeg。
    const QString preference = qEnvironmentVariable("MIDI_PLAY_VIDEO_ENCODER").trimmed().toLower();
    if (!preference.isEmpty() && preference != QStringLiteral("auto")
        && preference != QStringLiteral("h264_nvenc")) return {};
    const QString key = encoderCacheKey(executable, size, fps, crf, preference);
    {
        QMutexLocker lock(&encoderSelectionMutex);
        const auto cached = encoderSelectionCache.constFind(key);
        if (cached != encoderSelectionCache.constEnd()) return *cached;
    }
    // 外部探测放在全局缓存锁之外，避免其他导出任务被长时间持锁阻塞。
    const VideoEncoderSelection hardware {QStringLiteral("h264_nvenc"), true};
    const auto selected = probeVideoEncoder(executable, hardware, size, fps, crf, canceled)
        ? hardware : VideoEncoderSelection{};
    {
        QMutexLocker lock(&encoderSelectionMutex);
        encoderSelectionCache.insert(key, selected);
    }
    return selected;
}

// 优先使用 FFmpeg 同目录的 ffprobe，找不到时再查询 PATH。
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

// ffprobe 时间字段按字符串解析，拒绝缺失、非数值及 NaN/Inf。
bool readTimestamp(const QJsonObject& stream, const QString& key, double* value)
{
    if (!value) return false;
    bool ok = false;
    const double parsed = stream.value(key).toString().toDouble(&ok);
    if (!ok || !std::isfinite(parsed)) return false;
    *value = parsed;
    return true;
}

// 可选 ffprobe 元数据校验；未请求规格或无 ffprobe 时交由后续解码检查。
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
    // 校验流的呈现起点；AAC 预填充负 PTS、H.264 重排 DTS 可由 MP4 编辑列表补偿。
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
        // 部分 FFmpeg 构建暴露 AAC 最后一个填充包，音频时长容许一个 1024 样本包误差。
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
// 未正常结束时终止子进程并等待；临时文件清理由持有输出目录的应用服务负责。
FfmpegVideoEncoder::~FfmpegVideoEncoder()
{
    if (m_process.state() != QProcess::NotRunning) {
        m_process.kill();
        m_process.waitForFinished(3000);
    }
}
// 及时排空两个输出管道，错误信息仅保留末尾，防止长期任务积累无界日志。
void FfmpegVideoEncoder::collectDiagnostics()
{
    m_processDiagnostics += m_process.readAllStandardError();
    if (m_processDiagnostics.size() > 16384) m_processDiagnostics = m_processDiagnostics.right(16384);
    m_process.readAllStandardOutput();
}
// 先采集末尾诊断，再选择 stderr 或进程错误；error 按本类接口约定必须非空。
bool FfmpegVideoEncoder::processError(QString* error)
{
    collectDiagnostics();
    *error = QStringLiteral("FFmpeg 编码失败：%1").arg(m_processDiagnostics.isEmpty()
        ? m_process.errorString() : QString::fromUtf8(m_processDiagnostics).trimmed());
    return false;
}
// 以紧密 RGBA 和可选 float32 PCM 启动 CFR 编码；参数及音频长度检查在子进程启动前完成。
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
    // 每视频帧对应整数个采样帧，每采样帧为双声道各 4 字节；乘法前检查 qint64 溢出。
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
    m_processDiagnostics.clear();
    QElapsedTimer selectionTimer;
    selectionTimer.start();
    const auto encoder = selectVideoEncoder(executable, size, fps, crf, canceled);
    if (canceled && canceled->load()) return false;
    m_runtimeDiagnostics.videoEncoder = encoder.name;
    m_runtimeDiagnostics.hardwareAccelerated = encoder.hardwareAccelerated;
    m_runtimeDiagnostics.selectionNs = quint64(selectionTimer.nsecsElapsed());
    // 原始视频没有自带 PTS，输入帧率决定时间戳；所有参数通过 QStringList 传递，不经过 shell。
    QStringList args {QStringLiteral("-nostdin"), QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"), QStringLiteral("warning"),
        QStringLiteral("-f"), QStringLiteral("rawvideo"), QStringLiteral("-pixel_format"), QStringLiteral("rgba"),
        QStringLiteral("-video_size"), QStringLiteral("%1x%2").arg(size.width()).arg(size.height()),
        QStringLiteral("-framerate"), QString::number(fps), QStringLiteral("-i"), QStringLiteral("pipe:0")};
    if (!pcmPath.isEmpty()) args << QStringLiteral("-f") << QStringLiteral("f32le") << QStringLiteral("-ar")
        << QString::number(audioSampleRate) << QStringLiteral("-ac") << QStringLiteral("2") << QStringLiteral("-i") << pcmPath;
    args << QStringLiteral("-map") << QStringLiteral("0:v:0");
    args += videoEncodingArguments(encoder, crf, fps);
    if (!pcmPath.isEmpty()) args << QStringLiteral("-map") << QStringLiteral("1:a:0") << QStringLiteral("-c:a")
        << QStringLiteral("aac") << QStringLiteral("-b:a") << QStringLiteral("192k");
    // 两个原始输入从零开始且时长相同，各自读取到 EOF，不另设截断条件。
    // 保留 AAC 延迟包时间戳，由 MP4 编辑列表去掉预填充样本，保持音乐呈现起点。
    args << QStringLiteral("-avoid_negative_ts") << QStringLiteral("disabled")
        << QStringLiteral("-use_editlist") << QStringLiteral("1")
        << QStringLiteral("-movflags") << QStringLiteral("+faststart") << QStringLiteral("-f") << QStringLiteral("mp4") << output;
    m_process.start(executable, args);
    if (!m_process.waitForStarted(3000)) return processError(error);
    return true;
}
// 部分写入循环只在整帧成功交付后推进帧计数；QProcess 的待写缓存另设内存上限。
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
    constexpr qint64 maxPendingBytes = 4 * 1024 * 1024;
    constexpr qint64 writeChunkBytes = 1024 * 1024;
    QElapsedTimer stalled; stalled.start();
    while (remaining) {
        if (m_canceled && m_canceled->load()) return false;
        collectDiagnostics();
        if (m_process.state() != QProcess::Running) return processError(error);
        // QProcess 复制输入字节，写入完成后可回收源图像，无需每帧排空管道。
        // 未写出缓存最多 4 MiB，每次提交最多 1 MiB；finish 关闭输入并等待进程结束。
        const qint64 available = maxPendingBytes - m_process.bytesToWrite();
        if (available > 0) {
            const qint64 count = m_process.write(data, std::min({remaining, writeChunkBytes, available}));
            if (count < 0) return processError(error);
            data += count; remaining -= count;
            if (count > 0) continue;
        }
        if (m_process.waitForBytesWritten(50)) stalled.restart();
        if (stalled.elapsed() > 30000) { *error = QStringLiteral("FFmpeg 输入管道超时"); return false; }
    }
    ++m_writtenFrames;
    return true;
}
// 校验帧数后发送 EOF，等待编码器排空和封装；进程非零退出不能视为成功成品。
bool FfmpegVideoEncoder::finish(QString* error)
{
    if (m_writtenFrames != m_expectedFrames) {
        *error = QStringLiteral("视频帧数不足，无法完成封装");
        return false;
    }
    // 输入关闭发送 EOF，FFmpeg 排空缓存并完成编码及 MP4 索引收尾。
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
// 分别校验媒体规格和完整解码，识别容器可读但内部流截断或损坏的输出。
bool FfmpegVideoEncoder::validate(const QString& executable, const QString& path,
    const std::atomic_bool* canceled, QString* error, QSize expectedSize, int expectedFps,
    qint64 expectedFrames, bool expectAudio, qint64 expectedDurationUs,
    int expectedAudioSampleRate)
{
    if (canceled && canceled->load()) return false;
    if (!validateMetadata(executable, path, expectedSize, expectedFps, expectedFrames,
                          expectAudio, expectedDurationUs, expectedAudioSampleRate,
                          canceled, error)) return false;
    // -xerror 将解码错误视为失败；进度输出用于判断是否仍有处理活动。
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
        // 超时按无进度时长计算，持续输出进度的长视频不会因总解码时间超过阈值而失败。
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
