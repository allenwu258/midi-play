#pragma once

#include "app/introtextconfig.h"
#include <QWidget>

class QPlainTextEdit;
class QFontComboBox;
class QSpinBox;
class QComboBox;
class QDoubleSpinBox;

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
    QPlainTextEdit* m_introText = nullptr;
    QFontComboBox* m_introFont = nullptr;
    QSpinBox* m_introSize = nullptr;
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
