#pragma once

#include "infrastructure/encoding/ffmpegprobe.h"
#include <QObject>

namespace midi_play::app {
class SettingsService;

class FfmpegService final : public QObject {
    Q_OBJECT
public:
    explicit FfmpegService(SettingsService* settings, QObject* parent = nullptr);
    const encoding::FfmpegProbeResult& result() const { return m_result; }
    bool checking() const { return m_checking; }
    QString unavailableReason() const;
public slots:
    void refresh();
signals:
    void changed();
private:
    SettingsService* m_settings;
    encoding::FfmpegProbeResult m_result;
    quint64 m_revision = 0;
    bool m_checking = false;
};
} // namespace midi_play::app
