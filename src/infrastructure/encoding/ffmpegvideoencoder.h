#pragma once
// 同线程 FFmpeg 进程适配器：写入紧密 RGBA 帧，封装并验证视频成品。
#include <QImage>
#include <QProcess>
#include <QString>
#include <atomic>

namespace midi_play::encoding {
// 实际选择的编码器与探测耗时，供工作线程收集性能信息。
struct FfmpegVideoEncoderRuntimeDiagnostics {
    QString videoEncoder;
    bool hardwareAccelerated = false;
    quint64 selectionNs = 0;
};

// QProcess 必须在创建本对象的线程中使用；跨线程生产者通过 Worker 传递 QImage。
class FfmpegVideoEncoder final {
public:
    // 未正常结束时终止进程，不负责删除调用方拥有的临时输出。
    ~FfmpegVideoEncoder();
    // 校验参数和可选 PCM 长度，探测硬件编码可用性并启动真实编码进程。
    // error 需为有效指针；canceled 指针借用到任务结束。
    // size 是输出物理像素；frameCount/fps 定义 CFR 时长；PCM 为双声道交错 float32。
    // pcmPath 为空时只编码视频；输出是否最终替换已有文件由应用服务决定。
    bool open(const QString& executable, const QString& output, const QString& pcmPath,
              QSize size, int fps, int crf, qint64 frameCount, int audioSampleRate,
              const std::atomic_bool* canceled, QString* error);
    // 要求 RGBA8888、尺寸匹配且行跨度为 width*4，按有界块写入 stdin。
    bool writeFrame(const QImage& frame, QString* error);
    // 要求已写足期望帧数，关闭输入并等待封装完成，可取消且有超时限制。
    bool finish(QString* error);
    // 返回当前编码器选择快照。
    FfmpegVideoEncoderRuntimeDiagnostics diagnostics() const { return m_runtimeDiagnostics; }
    // ffprobe 可用时检查指定元数据，随后始终完整解码检查媒体有效性。
    // 未提供预期尺寸、帧率、帧数或时长时跳过相应匹配检查，时长单位为微秒。
    static bool validate(const QString& executable, const QString& path,
                         const std::atomic_bool* canceled, QString* error,
                         QSize expectedSize = {}, int expectedFps = 0,
                         qint64 expectedFrames = -1, bool expectAudio = false,
                         qint64 expectedDurationUs = -1, int expectedAudioSampleRate = 44'100);
private:
    // 持续排空输出，stderr 仅保留最近 16 KiB，避免进程管道因未读取而阻塞。
    void collectDiagnostics();
    // 汇总已采集诊断或 QProcess 错误并返回 false。
    bool processError(QString* error);
    QProcess m_process;
    const std::atomic_bool* m_canceled = nullptr; // 借用，编码及验证期间由调用方保持存活。
    QByteArray m_processDiagnostics;
    QSize m_size;
    qint64 m_expectedFrames = 0; // 与调用方的 CFR 时间线一致，少帧/多帧都拒绝。
    qint64 m_writtenFrames = 0;
    int m_audioSampleRate = 44'100;
    FfmpegVideoEncoderRuntimeDiagnostics m_runtimeDiagnostics;
};
} // namespace midi_play::encoding
