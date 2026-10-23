#pragma once

#include "app/introtextconfig.h"
#include <QWidget>

class QComboBox;
class QDoubleSpinBox;
class QFontComboBox;
class QPlainTextEdit;
class QSlider;
class QSpinBox;

namespace midi_play::presentation {

// Export-only editor. It owns a draft; callers decide when to accept that draft.
class IntroTextPanel final : public QWidget {
    Q_OBJECT
public:
    explicit IntroTextPanel(const app::IntroTextConfig& defaults, QWidget* parent = nullptr);
    app::IntroTextConfig config() const;
    void setPositionFromDrag(int x, int y);

signals:
    void configurationChanged(bool layoutChanged);

private:
    void changed(bool layoutChanged);

    bool m_updating = false;
    app::IntroTextConfig m_lastConfig;
    QPlainTextEdit* m_titleText = nullptr;
    QPlainTextEdit* m_bodyText = nullptr;
    QFontComboBox* m_introFont = nullptr;
    QSpinBox* m_titleSize = nullptr;
    QSpinBox* m_bodySize = nullptr;
    QSlider* m_outlineSlider = nullptr;
    QDoubleSpinBox* m_outlineSize = nullptr;
    QSpinBox* m_introWidth = nullptr;
    QComboBox* m_introAlignment = nullptr;
    QComboBox* m_introPosition = nullptr;
    QSpinBox* m_introX = nullptr;
    QSpinBox* m_introY = nullptr;
    QDoubleSpinBox* m_introDisplay = nullptr;
    QDoubleSpinBox* m_introExit = nullptr;
    QComboBox* m_introEffect = nullptr;
};
} // namespace midi_play::presentation
