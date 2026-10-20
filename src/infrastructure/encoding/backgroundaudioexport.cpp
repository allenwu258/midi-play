#include "backgroundaudioexport.h"
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QProcess>
#include <QStorageInfo>
#include <algorithm>

namespace midi_play::encoding {
namespace {
bool run(const QString& executable, const QStringList& arguments,
         const std::atomic_bool* canceled, QString* error)
{
    if (canceled && canceled->load()) return false;
    QProcess process;
    process.setProcessChannelMode(QProcess::MergedChannels);
    process.start(executable, QStringList {QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"),
        QStringLiteral("error"), QStringLiteral("-nostdin"), QStringLiteral("-y")} + arguments);
    if (!process.waitForStarted(3000)) { *error = process.errorString(); return false; }
    QByteArray diagnostics;
    QElapsedTimer timer; timer.start();
    while (!process.waitForFinished(50)) {
        diagnostics = (diagnostics + process.readAll()).right(8192);
        if ((canceled && canceled->load()) || timer.elapsed() > 30 * 60 * 1000) {
            process.kill(); process.waitForFinished(3000);
            *error = QStringLiteral("视频原声导出已取消或超时"); return false;
        }
    }
    diagnostics = (diagnostics + process.readAll()).right(8192);
    if (process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0) return true;
    *error = QStringLiteral("视频原声导出失败：%1").arg(QString::fromUtf8(diagnostics));
    return false;
}
}

bool mixBackgroundVideoAudio(const QString& executable, const QString& videoPath,
    const media::VideoMetadata& video, qint64 musicDurationUs, int ratePercent, int sampleRate,
    qint64 totalSamples, const QString& temporaryDirectory, QString& musicPcmPath,
    const std::atomic_bool* canceled, QString* error)
{
    const QDir directory(temporaryDirectory);
    const qint64 cycleSamples = std::max<qint64>(1,
        (std::min(video.durationUs, musicDurationUs) * sampleRate + 999'999) / 1'000'000);
    const qint64 audibleSamples = std::min(totalSamples,
        (musicDurationUs * sampleRate * 100 + qint64(ratePercent) * 1'000'000 - 1)
            / (qint64(ratePercent) * 1'000'000));
    const QStorageInfo storage(directory);
    if (storage.isValid() && storage.bytesAvailable() < (cycleSamples + totalSamples) * 8 + 64 * 1024 * 1024) {
        *error = QStringLiteral("磁盘空间不足以混合视频原声"); return false;
    }
    const QString cycle = directory.filePath(QStringLiteral("background-cycle.pcm"));
    const QString mixed = directory.filePath(QStringLiteral("mixed-audio.pcm"));
    const QString rate = QString::number(sampleRate);
    // Preserve source timestamps before translating to the video track origin.
    // first_pts trims audio preroll and inserts silence for delayed audio tracks.
    const QString cycleFilter = QStringLiteral("asetpts=PTS-(%1)/TB,aresample=%2:async=1:first_pts=0,atrim=end_sample=%3,apad=whole_len=%3")
        .arg(QString::number(video.startTimeUs / 1'000'000.0, 'f', 6)).arg(sampleRate).arg(cycleSamples);
    if (!run(executable, {QStringLiteral("-copyts"), QStringLiteral("-i"), videoPath, QStringLiteral("-map"), QStringLiteral("0:a:0"),
        QStringLiteral("-vn"), QStringLiteral("-af"), cycleFilter, QStringLiteral("-ac"), QStringLiteral("2"),
        QStringLiteral("-ar"), rate, QStringLiteral("-f"), QStringLiteral("f32le"), cycle}, canceled, error)) return false;
    QStringList arguments {QStringLiteral("-stream_loop"), QStringLiteral("-1"),
        QStringLiteral("-f"), QStringLiteral("f32le"), QStringLiteral("-ar"), rate,
        QStringLiteral("-ac"), QStringLiteral("2"), QStringLiteral("-i"), cycle};
    // Rate follows music time; trimming precedes padding so the background audio
    // is silent during the musical tail while the last video frame stays frozen.
    double speed = ratePercent / 100.0;
    QString tempo;
    while (speed < 0.5) { tempo += QStringLiteral("atempo=0.5,"); speed /= 0.5; }
    tempo += QStringLiteral("atempo=%1,").arg(speed, 0, 'f', 6);
    QString filter = QStringLiteral("[0:a]%1aresample=%2,atrim=end_sample=%3,apad=whole_len=%4[bg]")
        .arg(tempo).arg(sampleRate).arg(audibleSamples).arg(totalSamples);
    if (!musicPcmPath.isEmpty()) {
        arguments << QStringLiteral("-f") << QStringLiteral("f32le") << QStringLiteral("-ar") << rate
            << QStringLiteral("-ac") << QStringLiteral("2") << QStringLiteral("-i") << musicPcmPath;
        filter += QStringLiteral(";[1:a][bg]amix=inputs=2:duration=first:normalize=1[out]");
    } else {
        filter += QStringLiteral(";[bg]anull[out]");
    }
    arguments << QStringLiteral("-filter_complex") << filter << QStringLiteral("-map") << QStringLiteral("[out]")
        << QStringLiteral("-f") << QStringLiteral("f32le") << mixed;
    if (!run(executable, arguments, canceled, error)) return false;
    if (QFileInfo(mixed).size() != totalSamples * 8) {
        *error = QStringLiteral("混合音频长度与视频时间线不一致"); return false;
    }
    musicPcmPath = mixed;
    return true;
}
}
