#pragma once

#include <QMainWindow>

class QLabel;
class QPushButton;
class QSlider;

namespace midi_play::app { class PlayerApplicationService; }

namespace midi_play::presentation {

class MainWindow final : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(app::PlayerApplicationService* service, QWidget* parent = nullptr);

private slots:
    void openMusicXml();
    void openSoundFont();
    void updatePosition(qint64 position, qint64 duration);

private:
    app::PlayerApplicationService* m_service;
    QLabel* m_fileLabel;
    QLabel* m_soundFontLabel;
    QLabel* m_timeLabel;
    QLabel* m_statusLabel;
    QPushButton* m_playButton;
    QPushButton* m_pauseButton;
    QPushButton* m_stopButton;
    QSlider* m_positionSlider;
    bool m_sliderDragging = false;
};

} // namespace midi_play::presentation
