#pragma once
// 视频编码线程适配器：有界帧队列、QImage 回收及取消/收尾协调。

#include <QImage>
#include <QString>
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <mutex>
#include <thread>

namespace midi_play::encoding {

// 开启诊断时返回累计帧数/纳秒耗时/峰值队列深度及实际编码器信息。
struct FfmpegVideoEncoderDiagnostics {
    qint64 submittedFrames = 0; // 已入生产队列的帧数，不代表已编码或已保存。
    qint64 encodedFrames = 0; // 已成功交给 FFmpeg 输入管道的帧数。
    quint64 recycledFrameWaitNs = 0; // 生产者等待空闲图像的累计纳秒数。
    quint64 queueWaitNs = 0; // 提交帧时等待队列空位的累计纳秒数。
    quint64 writeNs = 0; // 向 FFmpeg 写入帧的累计纳秒数，包含管道反压。
    int peakQueueDepth = 0; // 等待消费的最大帧数，不包含正在写入的一帧。
    QString videoEncoder;
    bool hardwareAccelerated = false;
    quint64 encoderSelectionNs = 0;
};

// 专用线程独占编码器及 QProcess；单一生产者提交完整帧，有界队列控制内存并施加反压。
class FfmpegVideoEncoderWorker final {
public:
    // 队列容量至少为一，open 时额外分配生产者/消费者各一份帧缓存。
    explicit FfmpegVideoEncoderWorker(std::size_t capacity = 3);
    // 请求中止并唤醒等待者，join 后才允许成员资源销毁。
    ~FfmpegVideoEncoderWorker();

    FfmpegVideoEncoderWorker(const FfmpegVideoEncoderWorker&) = delete;
    FfmpegVideoEncoderWorker& operator=(const FfmpegVideoEncoderWorker&) = delete;

    // 仅允许启动一次；创建线程并等待编码进程启动结果。
    bool open(const QString& executable, const QString& output, const QString& pcmPath,
              QSize size, int fps, int crf, qint64 frameCount, int audioSampleRate,
              const std::atomic_bool* canceled, QString* error);
    // 单一生产者取得图像、填充再 move 给 submitFrame；字节复制进 QProcess 后回收图像。
    bool acquireFrame(QImage& frame, QString* error);
    // 校验帧格式，队列满时按 50 ms 周期等待并检查取消。
    bool submitFrame(QImage frame, QString* error);
    // 停止接收新帧，排空已提交队列并完成编码；与析构的立即中止语义不同。
    bool finish(QString* error);

    // 带锁读取真实提交/写入帧数；diagnostics 禁用时返回默认空统计。
    qint64 submittedFrames() const;
    qint64 encodedFrames() const;
    FfmpegVideoEncoderDiagnostics diagnostics() const;

private:
    // 消费线程入口，所有 QProcess 调用及编码器销毁均发生在该线程。
    void run(QString executable, QString output, QString pcmPath, QSize size, int fps, int crf,
             qint64 frameCount, int audioSampleRate, const std::atomic_bool* canceled);
    // 调用方持有 m_mutex，合并停止、错误和外部原子取消条件。
    bool shouldStopLocked() const;

    const std::size_t m_capacity;
    mutable std::mutex m_mutex; // 保护队列、启动结果、停止标志及统计快照。
    std::condition_variable m_notEmpty; // 消费者等待新帧或停止请求。
    std::condition_variable m_notFull; // 生产者等待队列空位或停止请求。
    std::condition_variable m_frameAvailable; // 生产者等待回收图像或停止请求。
    std::condition_variable m_started; // open 等待工作线程发布 FFmpeg 启动结果。
    std::deque<QImage> m_frames; // 等待编码的 FIFO，容量为 m_capacity。
    std::deque<QImage> m_recycledFrames; // 可供生产者重新填充的 RGBA 图像，move 后不再由池持有。
    QSize m_frameSize;
    std::thread m_thread;
    const std::atomic_bool* m_canceled = nullptr; // 借用，必须保持到工作线程 join。
    QString m_error;
    bool m_startedFlag = false; // 工作线程已发布启动结果，成功和失败都会置位。
    bool m_opened = false; // FFmpeg 曾成功打开；不表示工作线程当前仍在运行。
    bool m_stopRequested = false; // 拒绝新的生产请求，消费端仍可排空已有帧。
    bool m_abortRequested = false; // 立即丢弃待编码帧，禁止正常封装收尾。
    bool m_finishRequested = false; // 调用方明确要求排空队列后关闭 stdin 并完成封装。
    qint64 m_submittedFrames = 0;
    qint64 m_encodedFrames = 0;
    bool m_diagnosticsEnabled = false;
    quint64 m_recycledFrameWaitNs = 0;
    quint64 m_queueWaitNs = 0;
    quint64 m_writeNs = 0;
    int m_peakQueueDepth = 0;
    QString m_videoEncoder;
    bool m_hardwareAccelerated = false;
    quint64 m_encoderSelectionNs = 0;
};

} // namespace midi_play::encoding
