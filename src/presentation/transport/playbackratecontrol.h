#pragma once

#include "domain/settings/playersettings.h"

#include <QTimer>
#include <QToolButton>

class QFrame;
class QSlider;
class QSpinBox;

namespace midi_play::presentation {

// Owns the hover panel and its input lifecycle; playback remains the
// application service's responsibility. Model updates never emit user edits.
class PlaybackRateControl final : public QToolButton {
    Q_OBJECT
public:
    explicit PlaybackRateControl(QWidget* parent = nullptr);
    ~PlaybackRateControl() override;

    int ratePercent() const { return m_ratePercent; }

public slots:
    void setRatePercent(int percent);
    void setThemeMode(midi_play::settings::ThemeMode mode);

signals:
    void ratePercentEdited(int percent);

protected:
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void showPanel(bool focusEditor = false);
    void hidePanel(bool commitEditor = true);
    void hidePanelIfInactive();
    void applyUserRate(int percent);
    bool containsPointer() const;

    QFrame* m_panel = nullptr;
    QSlider* m_slider = nullptr;
    QSpinBox* m_editor = nullptr;
    QTimer m_hideTimer;
    int m_ratePercent = midi_play::settings::kDefaultPlaybackRatePercent;
};

} // namespace midi_play::presentation
