#pragma once
#include <QImage>
#include <QProcess>
#include <QString>
#include <atomic>

namespace midi_play::encoding {
struct FfmpegVideoEncoderRuntimeDiagnostics {
    QString videoEncoder;
    bool hardwareAccelerated = false;
    quint64 selectionNs = 0;
};

class FfmpegVideoEncoder final {
public:
    ~FfmpegVideoEncoder();
    bool open(const QString& executable, const QString& output, const QString& pcmPath,
              QSize size, int fps, int crf, qint64 frameCount, int audioSampleRate,
              const std::atomic_bool* canceled, QString* error);
    bool writeFrame(const QImage& frame, QString* error);
    bool finish(QString* error);
    FfmpegVideoEncoderRuntimeDiagnostics diagnostics() const { return m_runtimeDiagnostics; }
    static bool validate(const QString& executable, const QString& path,
                         const std::atomic_bool* canceled, QString* error,
                         QSize expectedSize = {}, int expectedFps = 0,
                         qint64 expectedFrames = -1, bool expectAudio = false,
                         qint64 expectedDurationUs = -1, int expectedAudioSampleRate = 44'100);
private:
    void collectDiagnostics();
    bool processError(QString* error);
    QProcess m_process;
    const std::atomic_bool* m_canceled = nullptr;
    QByteArray m_processDiagnostics;
    QSize m_size;
    qint64 m_expectedFrames = 0;
    qint64 m_writtenFrames = 0;
    int m_audioSampleRate = 44'100;
    FfmpegVideoEncoderRuntimeDiagnostics m_runtimeDiagnostics;
};
} // namespace midi_play::encoding
