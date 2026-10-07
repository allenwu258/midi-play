#pragma once

#include <QImage>
#include <QString>
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <mutex>
#include <thread>

namespace midi_play::encoding {

struct FfmpegVideoEncoderDiagnostics {
    qint64 submittedFrames = 0;
    qint64 encodedFrames = 0;
    quint64 queueWaitNs = 0;
    quint64 writeNs = 0;
    int peakQueueDepth = 0;
};

// Owns FfmpegVideoEncoder and its QProcess on one dedicated thread. The
// producer only submits complete frames; a bounded queue keeps memory use
// predictable and applies backpressure when the encoder falls behind.
class FfmpegVideoEncoderWorker final {
public:
    explicit FfmpegVideoEncoderWorker(std::size_t capacity = 3);
    ~FfmpegVideoEncoderWorker();

    FfmpegVideoEncoderWorker(const FfmpegVideoEncoderWorker&) = delete;
    FfmpegVideoEncoderWorker& operator=(const FfmpegVideoEncoderWorker&) = delete;

    bool open(const QString& executable, const QString& output, const QString& pcmPath,
              QSize size, int fps, int crf, qint64 frameCount, int audioSampleRate,
              const std::atomic_bool* canceled, QString* error);
    bool submitFrame(QImage frame, QString* error);
    bool finish(QString* error);

    qint64 submittedFrames() const;
    qint64 encodedFrames() const;
    FfmpegVideoEncoderDiagnostics diagnostics() const;

private:
    void run(QString executable, QString output, QString pcmPath, QSize size, int fps, int crf,
             qint64 frameCount, int audioSampleRate, const std::atomic_bool* canceled);
    bool shouldStopLocked() const;

    const std::size_t m_capacity;
    mutable std::mutex m_mutex;
    std::condition_variable m_notEmpty;
    std::condition_variable m_notFull;
    std::condition_variable m_started;
    std::deque<QImage> m_frames;
    std::thread m_thread;
    const std::atomic_bool* m_canceled = nullptr;
    QString m_error;
    bool m_startedFlag = false;
    bool m_opened = false;
    bool m_stopRequested = false;
    bool m_abortRequested = false;
    bool m_finishRequested = false;
    qint64 m_submittedFrames = 0;
    qint64 m_encodedFrames = 0;
    bool m_diagnosticsEnabled = false;
    quint64 m_queueWaitNs = 0;
    quint64 m_writeNs = 0;
    int m_peakQueueDepth = 0;
};

} // namespace midi_play::encoding
