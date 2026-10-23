#pragma once
#include "app/videoexportservice.h"
#include <QElapsedTimer>
#include <QWidget>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QSpinBox;
class QSlider;
class QPushButton;
class QTimer;

namespace midi_play::presentation {
class IntroTextPanel;
class IntroTextPreviewWidget;
class VideoExportPreviewRenderer;

class VideoExportPage final : public QWidget {
    Q_OBJECT
public:
    explicit VideoExportPage(const app::VideoExportOptions& defaults,
                             qint64 previewPositionUs, QWidget* parent = nullptr);
    app::VideoExportOptions options() const;
    QString outputPath() const;
    void setAvailability(bool available, const QString& reason);
    bool isAvailable() const noexcept { return m_available; }
    bool introTextEnabled() const;
    void setPageActive(bool active);
    QString validationError() const;
    int preferredContentHeight() const;

signals:
    void introTextPanelVisibilityChanged(bool visible);

protected:
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;

private:
    QString previewValidationError() const;
    app::IntroTextConfig introTextConfig() const;
    void updateScene();
    void updateIntroText();
    void updateIntroTextVisibility(bool enabled);
    void updatePreviewRange();
    void enterLayoutMode();
    void setPreviewFrame(qint64 frame, bool discontinuity);
    void startPreviewPlayback();
    void stopPreviewPlayback();
    qint64 frameAtOrAfter(qint64 milliseconds) const;

    app::VideoExportOptions m_defaults;
    QComboBox* m_size = nullptr;
    QCheckBox* m_retainMeasureGutter = nullptr;
    QComboBox* m_fps = nullptr;
    QComboBox* m_quality = nullptr;
    QSpinBox* m_rate = nullptr;
    QSpinBox* m_tail = nullptr;
    QCheckBox* m_audio = nullptr;
    QCheckBox* m_metronome = nullptr;
    QLineEdit* m_path = nullptr;
    QWidget* m_controls = nullptr;
    QWidget* m_leftContent = nullptr;
    QLabel* m_previewStatus = nullptr;
    QLabel* m_availabilityLabel = nullptr;
    QLabel* m_introError = nullptr;
    QLabel* m_introSummary = nullptr;
    IntroTextPreviewWidget* m_preview = nullptr;
    IntroTextPanel* m_introEditor = nullptr;
    QWidget* m_introPanel = nullptr;
    QWidget* m_previewTransport = nullptr;
    QCheckBox* m_introEnabled = nullptr;
    QPushButton* m_previewPlayButton = nullptr;
    QPushButton* m_previewMiddleButton = nullptr;
    QPushButton* m_previewEndButton = nullptr;
    QSlider* m_previewSlider = nullptr;
    QLabel* m_previewTimeLabel = nullptr;
    QTimer* m_previewTimer = nullptr;
    QTimer* m_textDebounce = nullptr;
    VideoExportPreviewRenderer* m_renderer = nullptr;
    qint64 m_originalMusicPosition = 0;
    qint64 m_previewFrame = 0;
    int m_previewFps = 60;
    qint64 m_lastPreviewFrame = 0;
    qint64 m_playbackStartFrame = 0;
    QElapsedTimer m_playbackClock;
    bool m_playing = false;
    bool m_layoutMode = true;
    bool m_available = true;
    bool m_pageActive = true;
    QString m_unavailableReason;
};
} // namespace midi_play::presentation
