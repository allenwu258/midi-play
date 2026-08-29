#pragma once

#include "domain/playback/playbacktypes.h"
#include "domain/visualization/visualchart.h"
#include "playbackmetadatapresenter.h"

#include <QMainWindow>
#include <QPointer>

class QLabel;
class QSlider;
class QToolButton;
class QResizeEvent;

namespace midi_play::app { class PlayerApplicationService; }
namespace midi_play::app { class SettingsService; }
namespace midi_play::presentation::settings { class SettingsDialog; }
namespace midi_play::presentation::visualization { class FallingNotesView; }

namespace midi_play::presentation {

class MainWindow final : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(app::PlayerApplicationService* service,
                        app::SettingsService* settingsService,
                        QWidget* parent = nullptr);

protected:
    void resizeEvent(QResizeEvent* event) override;

private slots:
    void openMusicFile();
    void openSoundFont();
    void showSettings();
    void updatePosition(qint64 position, qint64 duration);
    void updatePlaybackState(midi_play::playback::State state);

private:
    static QString formatTime(qint64 microseconds);
    void updateTimeDisplay(qint64 positionUs, qint64 durationUs);
    void updateMetadata(qint64 positionUs);
    void updateResponsiveVisibility();
    void updateTransportControls();

    static constexpr int kSliderResolution = 1'000'000;

    app::PlayerApplicationService* m_service = nullptr;
    app::SettingsService* m_settingsService = nullptr;
    QPointer<settings::SettingsDialog> m_settingsDialog;
    visualization::FallingNotesView* m_visualization = nullptr;
    QLabel* m_fileLabel = nullptr;
    QLabel* m_soundFontLabel = nullptr;
    QLabel* m_keyLabel = nullptr;
    QLabel* m_timeSignatureLabel = nullptr;
    QLabel* m_tempoLabel = nullptr;
    QLabel* m_timeLabel = nullptr;
    QLabel* m_statusLabel = nullptr;
    QToolButton* m_playButton = nullptr;
    QToolButton* m_pauseButton = nullptr;
    QToolButton* m_stopButton = nullptr;
    QSlider* m_positionSlider = nullptr;
    midi_play::visualization::VisualChartPtr m_chart;
    PlaybackMetadataTimeline m_metadataTimeline;
    playback::State m_playbackState = playback::State::Empty;
    qint64 m_positionUs = 0;
    qint64 m_durationUs = 0;
    bool m_sliderDragging = false;
    bool m_seekPending = false;
    qint64 m_pendingSeekUs = 0;
    qint64 m_displayedPositionSecond = -1;
    qint64 m_displayedDurationSecond = -1;
};

} // namespace midi_play::presentation
