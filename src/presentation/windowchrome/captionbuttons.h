#pragma once

#include "domain/settings/thememode.h"

#include <QWidget>
#include <array>

class QToolButton;

namespace midi_play::presentation::windowchrome {

enum class CaptionCommand { Minimize, MaximizeOrRestore, Close };

// Qt supplies painting and accessibility; WindowChromeController supplies the
// non-client input bridge. No HWND or Windows messages belong in this widget.
class CaptionButtons final : public QWidget {
    Q_OBJECT
public:
    explicit CaptionButtons(QWidget* parent = nullptr);
    QToolButton* button(CaptionCommand command) const noexcept;
    void setMetrics(int totalWidth, int height);
    void setTheme(midi_play::settings::ThemeMode theme);
    void setMaximized(bool maximized);
    void setWindowActive(bool active);
    void setNativeHovered(QToolButton* button);
    void setNativePressed(QToolButton* button);
    void setSystemColors(bool highContrast, const QColor& text,
                         const QColor& background, const QColor& highlight,
                         const QColor& highlightedText);

signals:
    void commandRequested(midi_play::presentation::windowchrome::CaptionCommand command);

private:
    std::array<QToolButton*, 3> m_buttons;
};

} // namespace midi_play::presentation::windowchrome
