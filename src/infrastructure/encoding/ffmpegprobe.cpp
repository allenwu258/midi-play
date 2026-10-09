// FFmpeg 探测实现：验证可执行文件身份、基础编码器及实际 MP4 输出能力。
#include "ffmpegprobe.h"

#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
#include <QRegularExpression>
#include <QTemporaryDir>

namespace midi_play::encoding {
namespace {

// 平台对应的程序名，供手动选择安装目录时补全。
QString executableName()
{
#if defined(Q_OS_WIN)
    return QStringLiteral("ffmpeg.exe");
#else
    return QStringLiteral("ffmpeg");
#endif
}

// 手动输入允许目录或可执行文件，规范化为绝对程序路径。
QString normalizeCandidate(const QString& configuredPath)
{
    const QString trimmed = configuredPath.trimmed();
    if (trimmed.isEmpty()) return {};
    const QFileInfo info(trimmed);
    if (info.isDir()) return info.absoluteFilePath() + QLatin1Char('/') + executableName();
    return info.absoluteFilePath();
}

// 取首行并限制为 240 字符，避免外部进程长输出充满 GUI 错误区域。
QString firstDiagnosticLine(const QByteArray& bytes)
{
    const QString text = QString::fromLocal8Bit(bytes).trimmed();
    const QString line = text.split(QRegularExpression(QStringLiteral("\\r?\\n")))
        .constFirst().trimmed();
    if (line.isEmpty()) return QStringLiteral("未知错误");
    constexpr int maximumLength = 240;
    return line.size() > maximumLength ? line.left(maximumLength - 3) + QStringLiteral("...") : line;
}

} // namespace

FfmpegProbeResult probeFfmpeg(const QString& configuredPath, bool usePath)
{
    FfmpegProbeResult result;
    result.executablePath = normalizeCandidate(configuredPath);
    if (usePath) {
        result.executablePath = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
    }
    if (result.executablePath.isEmpty()) {
        result.error = QStringLiteral("未找到 ffmpeg，请在设置中选择 ffmpeg 目录");
        return result;
    }

    const QFileInfo executable(result.executablePath);
    if (!executable.isFile() || !executable.isExecutable()) {
        result.error = QStringLiteral("ffmpeg 路径无效：%1").arg(result.executablePath);
        return result;
    }

    // 先验证版本输出及正常退出，避免把同名程序或损坏安装当作 FFmpeg。
    QProcess process;
    process.setProgram(result.executablePath);
    process.setArguments({QStringLiteral("-hide_banner"), QStringLiteral("-version")});
    process.setProcessChannelMode(QProcess::MergedChannels);
    process.start();
    if (!process.waitForStarted(1500)) {
        result.error = QStringLiteral("无法启动 ffmpeg：%1").arg(process.errorString());
        return result;
    }
    if (!process.waitForFinished(3000)) {
        process.kill();
        process.waitForFinished(500);
        result.error = QStringLiteral("ffmpeg 检查超时");
        return result;
    }
    const QString output = QString::fromLocal8Bit(process.readAll());
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        result.error = output.trimmed().isEmpty()
            ? QStringLiteral("ffmpeg 返回错误码 %1").arg(process.exitCode())
            : firstDiagnosticLine(output.toLocal8Bit());
        return result;
    }
    result.version = firstDiagnosticLine(output.toLocal8Bit());
    if (!output.trimmed().startsWith(QStringLiteral("ffmpeg version "))) {
        result.error = QStringLiteral("所选程序不是 FFmpeg");
        return result;
    }
    // 软件 H.264 和 AAC 是最低能力要求，硬件编码器另由正式导出路径探测。
    process.start(result.executablePath, {QStringLiteral("-hide_banner"), QStringLiteral("-encoders")});
    if (!process.waitForFinished(3000)) {
        process.kill();
        process.waitForFinished();
        result.error = QStringLiteral("FFmpeg 编码器检查超时");
        return result;
    }
    const QString encoders = QString::fromUtf8(process.readAll());
    if (process.exitCode() != 0
        || !encoders.contains(QRegularExpression(QStringLiteral("\\slibx264\\s")))
        || !encoders.contains(QRegularExpression(QStringLiteral("\\saac\\s")))) {
        result.error = QStringLiteral("FFmpeg 需要支持 libx264 和 AAC 编码");
        return result;
    }

    // 编码器列表只能证明已注册；实际短编码还会检测封装器、运行库及依赖是否可用。
    QTemporaryDir directory;
    if (!directory.isValid()) {
        result.error = QStringLiteral("无法创建 FFmpeg 检查临时目录");
        return result;
    }
    const QString smokeOutput = directory.filePath(QStringLiteral("probe.mp4"));
    QProcess smoke;
    smoke.setProcessChannelMode(QProcess::MergedChannels);
    smoke.start(result.executablePath, {
        QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"), QStringLiteral("error"),
        QStringLiteral("-y"), QStringLiteral("-f"), QStringLiteral("lavfi"),
        QStringLiteral("-i"), QStringLiteral("color=c=black:s=16x16:r=2:d=0.5"),
        QStringLiteral("-f"), QStringLiteral("lavfi"),
        QStringLiteral("-i"), QStringLiteral("anullsrc=r=48000:cl=stereo"),
        QStringLiteral("-t"), QStringLiteral("0.5"), QStringLiteral("-c:v"), QStringLiteral("libx264"),
        QStringLiteral("-pix_fmt"), QStringLiteral("yuv420p"), QStringLiteral("-c:a"), QStringLiteral("aac"),
        QStringLiteral("-movflags"), QStringLiteral("+faststart"), smokeOutput});
    if (!smoke.waitForStarted(1500) || !smoke.waitForFinished(5000)
        || smoke.exitStatus() != QProcess::NormalExit || smoke.exitCode() != 0
        || !QFileInfo::exists(smokeOutput) || QFileInfo(smokeOutput).size() == 0) {
        if (smoke.state() != QProcess::NotRunning) smoke.kill();
        smoke.waitForFinished(500);
        result.error = QStringLiteral("FFmpeg 无法完成 H.264/AAC MP4 检查：%1")
            .arg(firstDiagnosticLine(smoke.readAll()));
        return result;
    }
    result.valid = true;
    return result;
}

} // namespace midi_play::encoding
