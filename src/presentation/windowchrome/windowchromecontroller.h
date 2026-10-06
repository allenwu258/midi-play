#pragma once

#include "domain/settings/titlebarmode.h"
#include "domain/settings/thememode.h"

#include <QObject>
#include <QPointer>
#include <QVector>
#include <memory>

class QWidget;

namespace midi_play::presentation::windowchrome {
class CaptionButtons;

// GUI-thread owner of the native frame. No Win32 types or Qt private APIs leak
// into MainWindow. On other platforms this preserves the normal Qt frame.
class WindowChromeController final : public QObject {
    Q_OBJECT
public:
    explicit WindowChromeController(QWidget* window);
    ~WindowChromeController() override;

    void setTitleBar(QWidget* titleBar, QWidget* systemMenuWidget,
                     const QVector<QWidget*>& interactiveWidgets);
    void setCaptionButtons(CaptionButtons* buttons);
    void setMode(midi_play::settings::TitleBarMode mode);
    void setTheme(midi_play::settings::ThemeMode theme);
    midi_play::settings::TitleBarMode mode() const noexcept { return m_mode; }
    int captionButtonsWidth() const noexcept;
    int minimumCaptionHeight() const noexcept;

    bool processNativeEvent(const QByteArray& eventType, void* message, qintptr* result);
    void minimize();
    void maximizeOrRestore();
    void close();
    void showSystemMenu();

signals:
    void frameMetricsChanged();
    void modeChanged(midi_play::settings::TitleBarMode mode);
    void nativeIntegrationFailed(const QString& reason);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    struct NativeFrame;
    void refreshFrame();
    void scheduleRefresh();

    QPointer<QWidget> m_window;
    QPointer<QWidget> m_titleBar;
    QPointer<QWidget> m_systemMenuWidget;
    QPointer<CaptionButtons> m_captionButtons;
    QVector<QPointer<QWidget>> m_interactiveWidgets;
    midi_play::settings::TitleBarMode m_mode = midi_play::settings::TitleBarMode::Native;
    midi_play::settings::ThemeMode m_theme = midi_play::settings::kDefaultThemeMode;
    bool m_refreshPending = false;
    std::unique_ptr<NativeFrame> m_native;
};

} // namespace midi_play::presentation::windowchrome
