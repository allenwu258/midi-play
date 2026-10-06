#include "app/exporttimeline.h"
#include "infrastructure/encoding/ffmpegvideoencoder.h"

#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QTemporaryDir>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace {

constexpr int sampleRate = 44100;
constexpr int channels = 2;
constexpr int markerSamples = 1440;
constexpr qint64 markerTimesUs[] {0, 137'009, 641'007, 1'291'001, 1'969'019};

void require(bool condition, const char* message, const QString& detail = {})
{
    if (condition) return;
    std::fprintf(stderr, "FAILED: %s %s\n", message, detail.toUtf8().constData());
    std::exit(EXIT_FAILURE);
}

QByteArray run(const QString& executable, const QStringList& arguments)
{
    QProcess process;
    process.start(executable, arguments);
    require(process.waitForStarted(3000), "test process starts", process.errorString());
    require(process.waitForFinished(30000), "test process completes within timeout");
    const QByteArray diagnostics = process.readAllStandardError();
    require(process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0,
            "test process succeeds", QString::fromUtf8(diagnostics));
    return process.readAllStandardOutput();
}

std::vector<float> decodeAudio(const QString& ffmpeg, const QString& path)
{
    const QByteArray bytes = run(ffmpeg, {"-v", "error", "-nostdin", "-i", path,
        "-map", "0:a:0", "-f", "f32le", "-ar", "44100", "-ac", "2", "pipe:1"});
    require(bytes.size() % (sizeof(float) * channels) == 0, "decoded stereo PCM is complete");
    std::vector<float> samples(size_t(bytes.size()) / sizeof(float));
    std::memcpy(samples.data(), bytes.constData(), size_t(bytes.size()));
    return samples;
}

std::vector<float> referenceAudio(const midi_play::app::ExportTimeline& timeline)
{
    std::vector<float> samples(size_t(timeline.sampleCount()) * channels);
    int marker = 0;
    for (const qint64 time : markerTimesUs) {
        const qint64 first = timeline.eventSample(time);
        require(first + markerSamples <= timeline.sampleCount(), "marker fits the reference timeline");
        for (int i = 0; i < markerSamples; ++i) {
            const double envelope = std::pow(std::sin(3.141592653589793 * i / markerSamples), 2);
            for (int channel = 0; channel < channels; ++channel) {
                const double frequency = 613 + 137 * marker + 271 * channel;
                const double phase = 2 * 3.141592653589793 * i / sampleRate;
                samples[size_t(first + i) * channels + channel] = float(envelope
                    * (0.3 * std::sin(phase * frequency) + 0.08 * std::sin(phase * frequency * 1.713)));
            }
        }
        ++marker;
    }
    return samples;
}

void compareAudio(const std::vector<float>& expected, const std::vector<float>& decoded,
                  const midi_play::app::ExportTimeline& timeline)
{
    const qint64 expectedFrames = qint64(expected.size() / channels);
    const qint64 decodedFrames = qint64(decoded.size() / channels);
    require(std::abs(decodedFrames - expectedFrames) <= 16
            || (decodedFrames > expectedFrames && decodedFrames - expectedFrames < 1024),
            "AAC output duration matches PCM within sample rounding or one padded packet",
            QStringLiteral("decoded samples=%1, expected samples=%2")
                .arg(decodedFrames).arg(expectedFrames));
    const size_t comparedSamples = std::min(expected.size(), decoded.size());
    double energy = 0, error = 0;
    for (size_t i = 0; i < comparedSamples; ++i) {
        require(std::isfinite(decoded[i]), "decoded audio contains finite samples");
        energy += double(expected[i]) * expected[i];
        error += std::pow(double(expected[i]) - decoded[i], 2);
    }
    require(energy > 0 && error / energy < 0.02,
            "decoded audio matches the entire reference without duplicated or missing blocks",
            QStringLiteral("relative squared error=%1").arg(error / energy));

    int maximumLag = 0;
    double minimumCorrelation = 1;
    for (const qint64 time : markerTimesUs) {
        const qint64 first = timeline.eventSample(time);
        for (int channel = 0; channel < channels; ++channel) {
            double bestCorrelation = -1;
            int bestLag = 0;
            for (int lag = -1536; lag <= 1536; ++lag) {
                double referenceEnergy = 0, decodedEnergy = 0, product = 0;
                for (int i = 0; i < markerSamples; ++i) {
                    const float reference = expected[size_t(first + i) * channels + channel];
                    const qint64 position = first + i + lag;
                    const float actual = position < 0 || position >= qint64(decoded.size() / channels)
                        ? 0 : decoded[size_t(position) * channels + channel];
                    referenceEnergy += double(reference) * reference;
                    decodedEnergy += double(actual) * actual;
                    product += double(reference) * actual;
                }
                const double correlation = decodedEnergy > 0
                    ? product / std::sqrt(referenceEnergy * decodedEnergy) : 0;
                if (correlation > bestCorrelation) { bestCorrelation = correlation; bestLag = lag; }
            }
            maximumLag = std::max(maximumLag, std::abs(bestLag));
            minimumCorrelation = std::min(minimumCorrelation, bestCorrelation);
            require(bestCorrelation > 0.95 && std::abs(bestLag) <= 3,
                    "each channel's early/middle/late marker has no offset or drift",
                    QStringLiteral("time=%1 channel=%2 lag=%3 correlation=%4")
                        .arg(time).arg(channel).arg(bestLag).arg(bestCorrelation));
        }
    }

    // Exclude AAC pre/post-ringing around markers; every other sample must
    // remain silent. This catches extra copies that onset-only tests miss.
    double silentEnergy = 0;
    qint64 silentSamples = 0;
    for (qint64 i = 0; i < std::min(timeline.sampleCount(), decodedFrames); ++i) {
        bool nearMarker = false;
        for (const qint64 time : markerTimesUs) {
            const qint64 start = timeline.eventSample(time);
            nearMarker |= i >= start - 1024 && i < start + markerSamples + 1024;
        }
        if (nearMarker) continue;
        for (int channel = 0; channel < channels; ++channel) {
            silentEnergy += std::pow(decoded[size_t(i) * channels + channel], 2);
            ++silentSamples;
        }
    }
    require(silentSamples > 0 && silentEnergy / silentSamples < 0.000001,
            "silent gaps contain no unexpected repeated audio");
    std::fprintf(stdout, "  audio: max lag=%d samples, min correlation=%.6f, relative error=%.6f\n",
                 maximumLag, minimumCorrelation, error / energy);
}

void verifyVideoPts(const QString& ffprobe, const QString& path,
                    const midi_play::app::ExportTimeline& timeline, int fps)
{
    if (ffprobe.isEmpty()) return;
    const QByteArray json = run(ffprobe, {"-v", "error", "-select_streams", "v:0",
        "-show_frames", "-show_entries", "frame=best_effort_timestamp_time,pkt_dts_time", "-of", "json", path});
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(json, &error);
    require(error.error == QJsonParseError::NoError, "video frame metadata is JSON");
    const auto frames = document.object().value(QStringLiteral("frames")).toArray();
    require(frames.size() == timeline.frameCount(), "every encoded video frame is present");
    for (qsizetype i = 0; i < frames.size(); ++i) {
        bool ok = false;
        const double pts = frames[i].toObject().value(QStringLiteral("best_effort_timestamp_time"))
                               .toString().toDouble(&ok);
        require(ok && std::abs(pts - double(i) / fps) < 0.0001,
                "video presentation starts at zero and preserves CFR frame order");
        bool dtsOk = false;
        const double dts = frames[i].toObject().value(QStringLiteral("pkt_dts_time"))
                               .toString().toDouble(&dtsOk);
        require(dtsOk && dts >= -0.000001,
                "video decode timestamps are non-negative for broad MP4 player compatibility");
    }
}

void testEncoding(const QString& ffmpeg, const QString& ffprobe, const QString& directory,
                  int fps, int rate, bool includeAudio)
{
    const midi_play::app::ExportTimeline timeline(2'000'003, fps, rate, 123);
    const QString stem = QStringLiteral("%1-%2-%3").arg(fps).arg(rate).arg(includeAudio);
    const QString output = directory + QLatin1Char('/') + stem + QStringLiteral(".mp4");
    QString pcmPath;
    std::vector<float> expected;
    if (includeAudio) {
        expected = referenceAudio(timeline);
        pcmPath = directory + QLatin1Char('/') + stem + QStringLiteral(".pcm");
        QFile pcm(pcmPath);
        require(pcm.open(QIODevice::WriteOnly), "reference PCM is writable");
        const qint64 bytes = qint64(expected.size() * sizeof(float));
        require(pcm.write(reinterpret_cast<const char*>(expected.data()), bytes) == bytes,
                "reference PCM is complete");
    }
    QString error;
    midi_play::encoding::FfmpegVideoEncoder encoder;
    const QSize size(96, 64);
    require(encoder.open(ffmpeg, output, pcmPath, size, fps, 20, timeline.frameCount(), sampleRate, nullptr, &error),
            "production encoder opens", error);
    QImage frame(size, QImage::Format_RGBA8888);
    for (qint64 i = 0; i < timeline.frameCount(); ++i) {
        frame.fill(QColor(int(i * 17 % 200) + 20, int(i * 31 % 200) + 20, int(i * 47 % 200) + 20));
        require(encoder.writeFrame(frame, &error), "production encoder accepts frame", error);
    }
    require(!encoder.writeFrame(frame, &error), "encoder rejects an extra frame");
    require(encoder.finish(&error), "production encoder finalizes both streams", error);
    require(midi_play::encoding::FfmpegVideoEncoder::validate(ffmpeg, output, nullptr, &error,
                size, fps, timeline.frameCount(), includeAudio, timeline.videoDurationUs()),
            "production integrity check passes", error);
    verifyVideoPts(ffprobe, output, timeline, fps);
    if (includeAudio) compareAudio(expected, decodeAudio(ffmpeg, output), timeline);
    if (!ffprobe.isEmpty()) {
        require(!midi_play::encoding::FfmpegVideoEncoder::validate(ffmpeg, output, nullptr, &error,
                    size, fps, timeline.frameCount() + 1, includeAudio, timeline.videoDurationUs()),
                "integrity check rejects an incorrect frame count");
        if (fps == 30 && rate == 100 && includeAudio) {
            const QString shifted = directory + QStringLiteral("/shifted.mp4");
            run(ffmpeg, {"-v", "error", "-nostdin", "-itsoffset", "0.1", "-i", output,
                         "-map", "0", "-c", "copy", "-use_editlist", "1", shifted});
            require(!midi_play::encoding::FfmpegVideoEncoder::validate(ffmpeg, shifted, nullptr, &error,
                        size, fps, timeline.frameCount(), true, timeline.videoDurationUs()),
                    "integrity check rejects shifted presentation timestamps");
        }
    }
    std::fprintf(stdout, "PASS: %d FPS, %d%%, %s\n", fps, rate, includeAudio ? "stereo markers" : "silent");
}

void testInvalidPcm(const QString& ffmpeg, const QString& directory)
{
    const QString pcmPath = directory + QStringLiteral("/incomplete.pcm");
    QFile pcm(pcmPath);
    require(pcm.open(QIODevice::WriteOnly), "incomplete PCM is writable");
    pcm.write(QByteArray(8, '\0'));
    pcm.close();
    midi_play::encoding::FfmpegVideoEncoder encoder;
    QString error;
    require(!encoder.open(ffmpeg, directory + QStringLiteral("/invalid.mp4"), pcmPath,
                          QSize(96, 64), 30, 20, 30, sampleRate, nullptr, &error) && !error.isEmpty(),
            "encoder rejects an incomplete PCM before launching FFmpeg");
    require(encoder.open(ffmpeg, directory + QStringLiteral("/incomplete.mp4"), {},
                         QSize(96, 64), 30, 20, 30, sampleRate, nullptr, &error), "silent encoder opens", error);
    require(!encoder.finish(&error), "encoder rejects missing video frames");
}

} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    const QString ffmpeg = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
    if (ffmpeg.isEmpty()) {
        std::fprintf(stdout, "SKIP: FFmpeg is not on PATH\n");
        return 77;
    }
    const QString ffprobe = QStandardPaths::findExecutable(QStringLiteral("ffprobe"));
    QTemporaryDir directory;
    require(directory.isValid(), "encoder test directory exists");
    testInvalidPcm(ffmpeg, directory.path());
    for (const int fps : {30, 60}) {
        for (const int rate : {50, 100, 120, 150, 200}) {
            testEncoding(ffmpeg, ffprobe, directory.path(), fps, rate, true);
        }
        testEncoding(ffmpeg, ffprobe, directory.path(), fps, 150, false);
    }
    return 0;
}
