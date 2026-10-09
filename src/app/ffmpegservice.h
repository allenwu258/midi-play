#pragma once
// GUI 使用的异步 FFmpeg 能力缓存，响应设置变化并丢弃过期检测结果。

#include "infrastructure/encoding/ffmpegprobe.h"
#include <QObject>

namespace midi_play::app {
class SettingsService;

// 借用设置服务，探测任务只捕获路径/检测模式快照。
class FfmpegService final : public QObject {
    Q_OBJECT
public:
    // 连接路径和 PATH 模式变化，并立即触发首次检查。
    explicit FfmpegService(SettingsService* settings, QObject* parent = nullptr);
    // 当前结果与检测状态；检测期间结果会清空，不能作为上一次有效能力使用。
    const encoding::FfmpegProbeResult& result() const { return m_result; }
    bool checking() const { return m_checking; }
    // 检测中显示等待文本，否则返回探测失败原因。
    QString unavailableReason() const;
public slots:
    // 启动新代次探测，旧任务不会中断，但其结果不再写回。
    void refresh();
signals:
    // 检测开始和最新结果完成时均通知消费者重新读取快照。
    void changed();
private:
    SettingsService* m_settings;
    encoding::FfmpegProbeResult m_result;
    quint64 m_revision = 0;
    bool m_checking = false;
};
} // namespace midi_play::app
