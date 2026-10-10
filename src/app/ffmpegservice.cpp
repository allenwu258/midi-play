// FFmpeg 探测编排实现：后台运行外部进程，GUI 线程发布最新能力快照。
#include "ffmpegservice.h"
#include "settingsservice.h"
#include <QFutureWatcher>
#include <QtConcurrent>

namespace midi_play::app {

// 监听路径及搜索模式变化，并立即探测初始编码器能力。
FfmpegService::FfmpegService(SettingsService* settings, QObject* parent)
    : QObject(parent), m_settings(settings)
{
    if (settings) {
        connect(settings, &SettingsService::ffmpegPathChanged, this, &FfmpegService::refresh);
        connect(settings, &SettingsService::ffmpegUsePathChanged, this, &FfmpegService::refresh);
    }
    refresh();
}

QString FfmpegService::unavailableReason() const
{
    return m_checking ? QStringLiteral("正在检查 FFmpeg") : m_result.error;
}

// 发起独立后台探测，代次屏蔽旧结果；任务按值持有路径和模式快照。
void FfmpegService::refresh()
{
    // 每次检查先清空旧能力并发布 checking，消费者不会继续使用上一次有效路径。
    const quint64 revision = ++m_revision;
    m_checking = true;
    m_result = {};
    emit changed();
    const QString path = m_settings ? m_settings->ffmpegPath() : QString();
    const bool usePath = !m_settings || m_settings->ffmpegUsePath();
    auto* watcher = new QFutureWatcher<encoding::FfmpegProbeResult>(this);
    connect(watcher, &QFutureWatcher<encoding::FfmpegProbeResult>::finished, this, [this, watcher, revision] {
        const auto result = watcher->result();
        watcher->deleteLater();
        // 设置快速变更可产生并行探测，仅当前代次可以结束 checking 状态。
        if (revision != m_revision) return;
        m_result = result;
        m_checking = false;
        emit changed();
    });
    watcher->setFuture(QtConcurrent::run([path, usePath] { return encoding::probeFfmpeg(path, usePath); }));
}

} // namespace midi_play::app
