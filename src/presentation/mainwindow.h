#pragma once

#include "domain/playback/playbacktypes.h"

#include <QMainWindow>

class QLabel;
class QSlider;
class QToolButton;

namespace midi_play::app { class PlayerApplicationService; }
namespace midi_play::presentation::visualization { class FallingNotesView; }

namespace midi_play::presentation {

class MainWindow final : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(app::PlayerApplicationService* service, QWidget* parent = nullptr);

private slots:
    void openMusicFile();
    void openSoundFont();
    void updatePosition(qint64 position, qint64 duration);
    void updatePlaybackState(midi_play::playback::State state);

private:
    static QString formatTime(qint64 microseconds);
    void updateTransportControls();

    static constexpr int kSliderResolution = 1'000'000;

    app::PlayerApplicationService* m_service = nullptr;
    visualization::FallingNotesView* m_visualization = nullptr;
    QLabel* m_fileLabel = nullptr;
    QLabel* m_soundFontLabel = nullptr;
    QLabel* m_timeLabel = nullptr;
    QLabel* m_statusLabel = nullptr;
    QToolButton* m_playButton = nullptr;
    QToolButton* m_pauseButton = nullptr;
    QToolButton* m_stopButton = nullptr;
    QSlider* m_positionSlider = nullptr;
    playback::State m_playbackState = playback::State::Empty;
    qint64 m_positionUs = 0;
    qint64 m_durationUs = 0;
    bool m_sliderDragging = false;
    bool m_seekPending = false;
    qint64 m_pendingSeekUs = 0;
};

} // namespace midi_play::presentation
