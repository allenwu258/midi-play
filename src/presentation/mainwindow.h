#pragma once

#include "domain/playback/playbacktypes.h"
#include "domain/settings/titlebarmode.h"
#include "domain/visualization/visualchart.h"
#include "playbackmetadatapresenter.h"

#include <QMainWindow>
#include <QPointer>

class QLabel;
class QFrame;
class QSlider;
class QToolButton;
class QResizeEvent;
class QEvent;

namespace midi_play::app { class PlayerApplicationService; }
namespace midi_play::app { class SettingsService; }
namespace midi_play::presentation::settings { class SettingsDialog; }
namespace midi_play::presentation::windowchrome { class CustomTitleBar; }
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
    void changeEvent(QEvent* event) override;
    bool nativeEvent(const QByteArray& eventType, void* message, qintptr* result) override;

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
    void applyTitleBarMode(midi_play::settings::TitleBarMode mode);
    void updateWindowControlButtons();

    static constexpr int kSliderResolution = 1'000'000;

    app::PlayerApplicationService* m_service = nullptr;
    app::SettingsService* m_settingsService = nullptr;
    QPointer<settings::SettingsDialog> m_settingsDialog;
    midi_play::settings::TitleBarMode m_titleBarMode = midi_play::settings::kDefaultTitleBarMode;
    windowchrome::CustomTitleBar* m_topBar = nullptr;
    QFrame* m_windowControlsSeparator = nullptr;
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
    QToolButton* m_minimizeButton = nullptr;
    QToolButton* m_maximizeButton = nullptr;
    QToolButton* m_closeButton = nullptr;
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
