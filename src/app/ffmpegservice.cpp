#include "ffmpegservice.h"
#include "settingsservice.h"
#include <QFutureWatcher>
#include <QtConcurrent>

namespace midi_play::app {

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

void FfmpegService::refresh()
{
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
        if (revision != m_revision) return;
        m_result = result;
        m_checking = false;
        emit changed();
    });
    watcher->setFuture(QtConcurrent::run([path, usePath] { return encoding::probeFfmpeg(path, usePath); }));
}

} // namespace midi_play::app
