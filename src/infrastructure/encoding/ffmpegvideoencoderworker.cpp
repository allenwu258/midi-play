#include "ffmpegvideoencoderworker.h"
#include "ffmpegvideoencoder.h"

#include <QElapsedTimer>
#include <algorithm>
#include <exception>
#include <utility>

namespace midi_play::encoding {

FfmpegVideoEncoderWorker::FfmpegVideoEncoderWorker(std::size_t capacity)
    : m_capacity(std::max<std::size_t>(1, capacity))
    , m_diagnosticsEnabled(qEnvironmentVariableIntValue("MIDI_PLAY_VIDEO_DIAGNOSTICS") > 0)
{
}

FfmpegVideoEncoderWorker::~FfmpegVideoEncoderWorker()
{
    {
        std::lock_guard lock(m_mutex);
        m_abortRequested = true;
        m_stopRequested = true;
    }
    m_notEmpty.notify_all();
    m_notFull.notify_all();
    if (m_thread.joinable()) m_thread.join();
}

bool FfmpegVideoEncoderWorker::open(const QString& executable, const QString& output,
                                    const QString& pcmPath, QSize size, int fps, int crf,
                                    qint64 frameCount, int audioSampleRate,
                                    const std::atomic_bool* canceled, QString* error)
{
    {
        std::lock_guard lock(m_mutex);
        if (m_thread.joinable() || m_startedFlag) {
            if (error) *error = QStringLiteral("FFmpeg 编码工作线程已启动");
            return false;
        }
        m_canceled = canceled;
    }
    try {
        m_thread = std::thread(&FfmpegVideoEncoderWorker::run, this,
                               executable, output, pcmPath, size, fps, crf, frameCount,
                               audioSampleRate, canceled);
    } catch (const std::exception& exception) {
        if (error) *error = QString::fromUtf8(exception.what());
        return false;
    }
    std::unique_lock lock(m_mutex);
    m_started.wait(lock, [this] { return m_startedFlag; });
    if (!m_opened) {
        if (error) *error = m_error;
        lock.unlock();
        if (m_thread.joinable()) m_thread.join();
        return false;
    }
    return true;
}

bool FfmpegVideoEncoderWorker::submitFrame(QImage frame, QString* error)
{
    if (frame.isNull()) {
        if (error) *error = QStringLiteral("视频帧为空");
        return false;
    }
    std::unique_lock lock(m_mutex);
    if (!m_startedFlag || !m_opened) {
        if (error) *error = m_error.isEmpty() ? QStringLiteral("FFmpeg 编码工作线程未启动") : m_error;
        return false;
    }
    QElapsedTimer queueTimer;
    if (m_diagnosticsEnabled) queueTimer.start();
    m_notFull.wait(lock, [this] {
        return m_frames.size() < m_capacity || shouldStopLocked();
    });
    if (m_diagnosticsEnabled) m_queueWaitNs += quint64(queueTimer.nsecsElapsed());
    if (shouldStopLocked()) {
        if (error) *error = m_error;
        return false;
    }
    m_frames.push_back(std::move(frame));
    ++m_submittedFrames;
    if (m_diagnosticsEnabled) m_peakQueueDepth = std::max(m_peakQueueDepth, int(m_frames.size()));
    lock.unlock();
    m_notEmpty.notify_one();
    return true;
}

bool FfmpegVideoEncoderWorker::finish(QString* error)
{
    {
        std::lock_guard lock(m_mutex);
        if (!m_opened) {
            if (error) *error = m_error.isEmpty() ? QStringLiteral("FFmpeg 编码工作线程未启动") : m_error;
            return false;
        }
        m_finishRequested = true;
        m_stopRequested = true;
    }
    m_notEmpty.notify_all();
    m_notFull.notify_all();
    if (m_thread.joinable()) m_thread.join();

    std::lock_guard lock(m_mutex);
    if (error) *error = m_error;
    return m_error.isEmpty() && !m_abortRequested && (!m_canceled || !m_canceled->load());
}

qint64 FfmpegVideoEncoderWorker::submittedFrames() const
{
    std::lock_guard lock(m_mutex);
    return m_submittedFrames;
}

qint64 FfmpegVideoEncoderWorker::encodedFrames() const
{
    std::lock_guard lock(m_mutex);
    return m_encodedFrames;
}

FfmpegVideoEncoderDiagnostics FfmpegVideoEncoderWorker::diagnostics() const
{
    std::lock_guard lock(m_mutex);
    if (!m_diagnosticsEnabled) return {};
    return {m_submittedFrames, m_encodedFrames, m_queueWaitNs, m_writeNs, m_peakQueueDepth};
}

void FfmpegVideoEncoderWorker::run(QString executable, QString output, QString pcmPath,
                                   QSize size, int fps, int crf, qint64 frameCount,
                                   int audioSampleRate, const std::atomic_bool* canceled)
{
    FfmpegVideoEncoder encoder;
    QString error;
    const bool opened = encoder.open(executable, output, pcmPath, size, fps, crf,
                                     frameCount, audioSampleRate, canceled, &error);
    {
        std::lock_guard lock(m_mutex);
        m_startedFlag = true;
        m_opened = opened;
        if (!opened) m_error = error;
    }
    m_started.notify_all();
    if (!opened) return;

    for (;;) {
        QImage frame;
        {
            std::unique_lock lock(m_mutex);
            m_notEmpty.wait(lock, [this] {
                return !m_frames.empty() || m_stopRequested || m_abortRequested;
            });
            if (m_abortRequested || (canceled && canceled->load())) {
                m_frames.clear();
                break;
            }
            if (m_frames.empty()) break;
            frame = std::move(m_frames.front());
            m_frames.pop_front();
        }
        m_notFull.notify_one();

        error.clear();
        QElapsedTimer writeTimer;
        if (m_diagnosticsEnabled) writeTimer.start();
        if (!encoder.writeFrame(frame, &error)) {
            std::lock_guard lock(m_mutex);
            if (!error.isEmpty()) m_error = error;
            m_stopRequested = true;
            m_frames.clear();
            m_notFull.notify_all();
            break;
        }
        if (m_diagnosticsEnabled) m_writeNs += quint64(writeTimer.nsecsElapsed());
        {
            std::lock_guard lock(m_mutex);
            ++m_encodedFrames;
        }
        if (canceled && canceled->load()) {
            std::lock_guard lock(m_mutex);
            m_abortRequested = true;
            m_stopRequested = true;
            m_frames.clear();
            m_notFull.notify_all();
            break;
        }
    }

    bool finishRequested = false;
    bool abortRequested = false;
    {
        std::lock_guard lock(m_mutex);
        finishRequested = m_finishRequested;
        abortRequested = m_abortRequested;
    }
    if (finishRequested && !abortRequested && (!canceled || !canceled->load())) {
        error.clear();
        if (!encoder.finish(&error)) {
            std::lock_guard lock(m_mutex);
            if (!error.isEmpty()) m_error = error;
        }
    }
}

bool FfmpegVideoEncoderWorker::shouldStopLocked() const
{
    return m_stopRequested || m_abortRequested || !m_error.isEmpty()
        || (m_canceled && m_canceled->load());
}

} // namespace midi_play::encoding
