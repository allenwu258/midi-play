#pragma once

#include "domain/playback/playbacktypes.h"
#include "domain/visualization/visualchart.h"
#include "domain/visualization/playbackscenestate.h"
#include "visualplaybackclock.h"

#include <QVulkanWindow>
#include <QFont>

namespace midi_play::presentation::visualization {

class FallingNotesVulkanWindow final : public QVulkanWindow {
    Q_OBJECT
public:
    explicit FallingNotesVulkanWindow(QWindow* parent = nullptr);

    void setChart(midi_play::visualization::VisualChartPtr chart);
    void setTransportPosition(qint64 positionUs, qint64 durationUs);
    void setTransportState(midi_play::playback::State state);
    void setShowNotationStrip(bool show);
    midi_play::visualization::VisualChartPtr chart() const { return m_chart; }
    qint64 positionUs() const noexcept { return m_positionUs; }
    qint64 durationUs() const noexcept { return m_durationUs; }
    midi_play::playback::State transportState() const noexcept { return m_state; }
    midi_play::visualization::PlaybackSceneState sceneState() const;
    QFont sceneFont() const { return m_font; }
    void setSceneFont(const QFont& font) { m_font = font; requestUpdate(); }
    void setLoading(bool value) { m_loading = value; if (value) m_error.clear(); requestUpdate(); }
    void setErrorMessage(const QString& message) { m_error = message; m_loading = false; requestUpdate(); }
    void frameCompleted(bool success);
    void setVisualClock(const VisualPlaybackClock* clock) { m_visualClock = clock; }
    void setEffectsStart(qint64 time) { m_effectsStartUs = time; }

    QVulkanWindowRenderer* createRenderer() override;

signals:
    void initializationFailed(const QString& message);
    void frameRendered();

private:
    class Renderer;
    midi_play::visualization::VisualChartPtr m_chart;
    qint64 m_positionUs = 0;
    qint64 m_durationUs = 0;
    midi_play::playback::State m_state = midi_play::playback::State::Empty;
    bool m_loading = false;
    bool m_showNotationStrip = false;
    QString m_error;
    QFont m_font;
    const VisualPlaybackClock* m_visualClock = nullptr;
    qint64 m_effectsStartUs = 0;
};

} // namespace midi_play::presentation::visualization
