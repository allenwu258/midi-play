#include "windowchromecontroller.h"

#include "windowchromegeometry.h"
#include "captionbuttons.h"
#include "presentation/theme/apptheme.h"

#include <QEvent>
#include <QGuiApplication>
#include <QResizeEvent>
#include <QStyle>
#include <QTimer>
#include <QToolButton>
#include <QWidget>
#include <QWindow>
#include <QVariant>
#include <QtMath>

#if defined(Q_OS_WIN)
#include <windows.h>
#include <windowsx.h>
#include <dwmapi.h>
#include <shellapi.h>
#endif

#include <algorithm>
#include <array>

namespace midi_play::presentation::windowchrome {

struct WindowChromeController::NativeFrame {
    explicit NativeFrame(WindowChromeController& controller) : owner(controller) {}

    WindowChromeController& owner;
    int buttonsWidth = 0;
    int captionHeight = 0;

#if defined(Q_OS_WIN)
    struct Attribute {
        DWORD id;
        DWORD value = 0;
        bool captured = false;
    };
    std::array<Attribute, 6> attributes {{
        {DWMWA_NCRENDERING_POLICY}, {DWMWA_USE_IMMERSIVE_DARK_MODE},
        {DWMWA_WINDOW_CORNER_PREFERENCE}, {DWMWA_CAPTION_COLOR},
        {DWMWA_TEXT_COLOR}, {DWMWA_BORDER_COLOR},
    }};
    HWND hwnd = nullptr;
    UINT dpi = USER_DEFAULT_SCREEN_DPI;
    bool customApplied = false;
    QPointer<QToolButton> pressedButton;

    QToolButton* captionButtonAt(POINT screenPoint) const
    {
        if (!owner.m_captionButtons || !owner.m_captionButtons->isVisibleTo(owner.m_window)) return nullptr;
        RECT window {};
        if (!GetWindowRect(hwnd, &window)) return nullptr;
        const QPointF point(screenPoint.x - window.left, screenPoint.y - window.top);
        for (auto command : {CaptionCommand::Minimize, CaptionCommand::MaximizeOrRestore, CaptionCommand::Close}) {
            auto* button = owner.m_captionButtons->button(command);
            if (button->isEnabled() && widgetRect(button).contains(point)) return button;
        }
        return nullptr;
    }

    void clearCaptionInput()
    {
        pressedButton = nullptr;
        if (owner.m_captionButtons) {
            owner.m_captionButtons->setNativeHovered(nullptr);
            owner.m_captionButtons->setNativePressed(nullptr);
        }
        if (hwnd && GetCapture() == hwnd) ReleaseCapture();
    }

    bool available() const noexcept
    {
        // Offscreen WIds are opaque tokens, not HWNDs. Never pass them to Win32.
        return QGuiApplication::platformName() == QLatin1String("windows");
    }

    qreal scale() const noexcept
    {
        const auto* window = owner.m_window ? owner.m_window->windowHandle() : nullptr;
        return window ? window->devicePixelRatio() : qreal(dpi) / USER_DEFAULT_SCREEN_DPI;
    }

    int frameX() const { return GetSystemMetricsForDpi(SM_CXSIZEFRAME, dpi)
                            + GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi); }
    int frameY() const { return GetSystemMetricsForDpi(SM_CYSIZEFRAME, dpi)
                            + GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi); }

    void bind(HWND handle)
    {
        if (hwnd == handle) return;
        clearCaptionInput();
        hwnd = handle;
        customApplied = false;
        for (auto& attribute : attributes) attribute.captured = false;
    }

    void captureAttributes()
    {
        for (auto& attribute : attributes) {
            attribute.captured = SUCCEEDED(DwmGetWindowAttribute(
                hwnd, attribute.id, &attribute.value, sizeof(attribute.value)));
        }
    }

    void restoreAttributes()
    {
        clearCaptionInput();
        if (!customApplied || !IsWindow(hwnd)) return;
        const MARGINS margins {};
        DwmExtendFrameIntoClientArea(hwnd, &margins);
        for (const auto& attribute : attributes) {
            if (attribute.captured)
                DwmSetWindowAttribute(hwnd, attribute.id, &attribute.value, sizeof(attribute.value));
        }
        customApplied = false;
    }

    bool fullScreen() const { return owner.m_window && owner.m_window->isFullScreen(); }

    void applyAppearance()
    {
        const DWMNCRENDERINGPOLICY rendering = DWMNCRP_ENABLED;
        DwmSetWindowAttribute(hwnd, DWMWA_NCRENDERING_POLICY, &rendering, sizeof(rendering));
        HIGHCONTRASTW contrast {sizeof(HIGHCONTRASTW)};
        SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(contrast), &contrast, 0);
        const bool highContrast = (contrast.dwFlags & HCF_HIGHCONTRASTON) != 0;
        if (owner.m_captionButtons) {
            const auto systemColor = [](int index) {
                const COLORREF color = GetSysColor(index);
                return QColor(GetRValue(color), GetGValue(color), GetBValue(color));
            };
            owner.m_captionButtons->setSystemColors(highContrast, systemColor(COLOR_WINDOWTEXT),
                systemColor(COLOR_WINDOW), systemColor(COLOR_HIGHLIGHT), systemColor(COLOR_HIGHLIGHTTEXT));
        }
        if (owner.m_titleBar) {
            QPalette palette = owner.m_window->palette();
            if (highContrast) {
                const auto systemColor = [](int index) {
                    const COLORREF color = GetSysColor(index);
                    return QColor(GetRValue(color), GetGValue(color), GetBValue(color));
                };
                palette.setColor(QPalette::Window, systemColor(COLOR_WINDOW));
                palette.setColor(QPalette::WindowText, systemColor(COLOR_WINDOWTEXT));
                palette.setColor(QPalette::ButtonText, systemColor(COLOR_BTNTEXT));
                palette.setColor(QPalette::Highlight, systemColor(COLOR_HIGHLIGHT));
                palette.setColor(QPalette::HighlightedText, systemColor(COLOR_HIGHLIGHTTEXT));
            }
            owner.m_titleBar->setPalette(palette);
            if (owner.m_titleBar->property("nativeHighContrast").toBool() != highContrast) {
                owner.m_titleBar->setProperty("nativeHighContrast", highContrast);
                owner.m_titleBar->style()->unpolish(owner.m_titleBar);
                owner.m_titleBar->style()->polish(owner.m_titleBar);
                for (auto* widget : owner.m_titleBar->findChildren<QWidget*>()) {
                    widget->style()->unpolish(widget);
                    widget->style()->polish(widget);
                    widget->update();
                }
            }
        }
        const BOOL dark = !highContrast && owner.m_theme == midi_play::settings::ThemeMode::Dark;
        DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));

        const auto& theme = theme::themeFor(owner.m_theme);
        const auto nativeColor = [](const QColor& color) {
            return RGB(color.red(), color.green(), color.blue());
        };
        const COLORREF caption = highContrast ? DWMWA_COLOR_DEFAULT : nativeColor(theme.widgets.panel);
        const COLORREF text = highContrast ? DWMWA_COLOR_DEFAULT : nativeColor(theme.widgets.text);
        const COLORREF border = DWMWA_COLOR_DEFAULT;
        DwmSetWindowAttribute(hwnd, DWMWA_CAPTION_COLOR, &caption, sizeof(caption));
        DwmSetWindowAttribute(hwnd, DWMWA_TEXT_COLOR, &text, sizeof(text));
        DwmSetWindowAttribute(hwnd, DWMWA_BORDER_COLOR, &border, sizeof(border));
        const DWM_WINDOW_CORNER_PREFERENCE corner = DWMWCP_ROUND;
        DwmSetWindowAttribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &corner, sizeof(corner));
        // Unsupported Windows 11 attributes return E_INVALIDARG on Windows 10.
        // The Windows 10 border remains available without Windows 11 attributes.
    }

    QString apply()
    {
        if (!available() || !owner.m_window) return {};
        bind(reinterpret_cast<HWND>(owner.m_window->winId()));
        const UINT currentDpi = GetDpiForWindow(hwnd);
        if (currentDpi) dpi = currentDpi;

        const bool custom = owner.m_mode == midi_play::settings::TitleBarMode::Custom;
        if (custom) {
            if (fullScreen()) clearCaptionInput();
            BOOL composition = FALSE;
            if (FAILED(DwmIsCompositionEnabled(&composition)) || !composition)
                return QStringLiteral("DWM 不可用，已使用原生标题栏");
            if (!fullScreen()) {
                const LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_STYLE);
                const LONG_PTR required = WS_CAPTION | WS_THICKFRAME | WS_SYSMENU;
                if ((style & required) != required)
                    return QStringLiteral("窗口未提供原生框架能力，已使用原生标题栏");
            }
            if (!customApplied) captureAttributes();
            customApplied = true;
            applyAppearance();
        } else {
            restoreAttributes();
        }

        // Preserve HWND, activation, z-order and WINDOWPLACEMENT. Qt 6.8 reads
        // the resulting NCCALCSIZE margins into its public frame geometry.
        if (!SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
                          SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE
                          | SWP_NOOWNERZORDER | SWP_FRAMECHANGED)) {
            return QStringLiteral("无法刷新 Windows 窗口框架（%1），已使用原生标题栏")
                .arg(GetLastError());
        }

        if (custom) {
            const int height = owner.m_titleBar
                ? qCeil(owner.m_titleBar->height() * scale()) + frameY() : frameY();
            const MARGINS margins {0, 0, fullScreen() ? 0 : height, 0};
            if (FAILED(DwmExtendFrameIntoClientArea(hwnd, &margins)))
                return QStringLiteral("无法扩展 DWM 标题区域，已使用原生标题栏");
        }
        updateMetrics();
        return {};
    }

    void updateMetrics()
    {
        int width = 0;
        int height = 0;
        if (owner.m_mode == midi_play::settings::TitleBarMode::Custom && !fullScreen()) {
            width = buttonsWidth;
            height = qCeil((GetSystemMetricsForDpi(SM_CYCAPTION, dpi) + frameY()) / scale());
            RECT buttons {}, window {}, client {};
            POINT origin {};
            if (!IsIconic(hwnd) && SUCCEEDED(DwmGetWindowAttribute(
                    hwnd, DWMWA_CAPTION_BUTTON_BOUNDS, &buttons, sizeof(buttons)))
                && buttons.right > buttons.left && buttons.bottom > buttons.top
                && GetWindowRect(hwnd, &window) && GetClientRect(hwnd, &client)
                && ClientToScreen(hwnd, &origin)) {
                // Caption bounds are window-relative; Qt contents start at the
                // real client origin, which differs in normal/maximized states.
                width = qCeil((origin.x + client.right - window.left - buttons.left) / scale());
                height = std::max(height, qCeil((window.top + buttons.bottom - origin.y) / scale()));
            } else if (width == 0) {
                width = qCeil((3 * GetSystemMetricsForDpi(SM_CXSIZE, dpi) + frameX()) / scale());
            }
        }
        width = std::max(0, width);
        if (owner.m_captionButtons && width > 0) {
            owner.m_captionButtons->setMetrics(width, height);
            owner.m_captionButtons->setMaximized(owner.m_window->isMaximized());
        }
        if (buttonsWidth != width || captionHeight != height) {
            buttonsWidth = width;
            captionHeight = height;
            emit owner.frameMetricsChanged();
        }
    }

    void leaveAutoHideTaskbarRevealArea(RECT& rect) const
    {
        MONITORINFO monitor {sizeof(MONITORINFO)};
        if (!GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &monitor)) return;
        for (UINT edge : {ABE_LEFT, ABE_TOP, ABE_RIGHT, ABE_BOTTOM}) {
            APPBARDATA bar {sizeof(APPBARDATA)};
            bar.uEdge = edge;
            bar.rc = monitor.rcMonitor;
            if (!SHAppBarMessage(ABM_GETAUTOHIDEBAREX, &bar)) continue;
            // Only reserve an edge that is not already excluded by rcWork.
            constexpr LONG revealPixels = 2;
            if (edge == ABE_LEFT && rect.left <= monitor.rcMonitor.left) rect.left += revealPixels;
            if (edge == ABE_TOP && rect.top <= monitor.rcMonitor.top) rect.top += revealPixels;
            if (edge == ABE_RIGHT && rect.right >= monitor.rcMonitor.right) rect.right -= revealPixels;
            if (edge == ABE_BOTTOM && rect.bottom >= monitor.rcMonitor.bottom) rect.bottom -= revealPixels;
        }
    }

    LRESULT calculateClientRect(const MSG& message) const
    {
        RECT& client = message.wParam
            ? reinterpret_cast<NCCALCSIZE_PARAMS*>(message.lParam)->rgrc[0]
            : *reinterpret_cast<RECT*>(message.lParam);
        const RECT proposed = client;
        if (fullScreen()) return 0;

        // Preserve Windows' left/right/bottom frame calculation. Only the
        // caption is extended into the client. Maximized windows extend their
        // outer thick frame beyond rcWork; remove that inset on the top too.
        DefWindowProcW(hwnd, message.message, message.wParam, message.lParam);
        UINT visibleBorder = std::max(1, MulDiv(1, dpi, USER_DEFAULT_SCREEN_DPI));
        DwmGetWindowAttribute(hwnd, DWMWA_VISIBLE_FRAME_BORDER_THICKNESS,
                              &visibleBorder, sizeof(visibleBorder));
        client.top = proposed.top + (IsZoomed(hwnd) ? frameY() : LONG(visibleBorder));
        if (IsZoomed(hwnd)) leaveAutoHideTaskbarRevealArea(client);
        return 0;
    }

    QRectF widgetRect(const QWidget* widget) const
    {
        if (!widget || !widget->isVisibleTo(owner.m_window)) return {};
        RECT window {};
        POINT origin {};
        if (!GetWindowRect(hwnd, &window) || !ClientToScreen(hwnd, &origin)) return {};
        const QPoint position = widget->mapTo(owner.m_window, QPoint());
        return QRectF(origin.x - window.left + position.x() * scale(),
                      origin.y - window.top + position.y() * scale(),
                      widget->width() * scale(), widget->height() * scale());
    }

    LRESULT hitTest(const MSG& message) const
    {
        if (fullScreen()) return HTCLIENT;
        POINT screenPoint {GET_X_LPARAM(message.lParam), GET_Y_LPARAM(message.lParam)};
        if (auto* button = captionButtonAt(screenPoint)) {
            if (button == owner.m_captionButtons->button(CaptionCommand::Minimize)) return HTMINBUTTON;
            if (button == owner.m_captionButtons->button(CaptionCommand::MaximizeOrRestore)) return HTMAXBUTTON;
            return HTCLOSE;
        }

        RECT rect {};
        if (!GetWindowRect(hwnd, &rect)) return HTCLIENT;
        FrameGeometry geometry;
        geometry.window = QRectF(0, 0, rect.right - rect.left, rect.bottom - rect.top);
        geometry.caption = widgetRect(owner.m_titleBar);
        geometry.systemMenu = widgetRect(owner.m_systemMenuWidget);
        for (const auto& widget : owner.m_interactiveWidgets) {
            // Disabled controls are still client areas, never draggable chrome.
            const QRectF region = widgetRect(widget);
            if (!region.isEmpty()) geometry.interactive.append(region);
        }
        // A disabled caption control remains an ordinary, non-draggable client
        // region. Its native command must never be emitted by hit testing.
        const QRectF controls = widgetRect(owner.m_captionButtons);
        if (!controls.isEmpty()) geometry.interactive.append(controls);
        geometry.resizeBorder = QMargins(frameX(), frameY(), frameX(), frameY());
        geometry.maximized = IsZoomed(hwnd);
        geometry.resizeHorizontal = owner.m_window->minimumWidth() < owner.m_window->maximumWidth();
        geometry.resizeVertical = owner.m_window->minimumHeight() < owner.m_window->maximumHeight();
        const QPointF point(GET_X_LPARAM(message.lParam) - rect.left,
                            GET_Y_LPARAM(message.lParam) - rect.top);
        switch (hitTestFrame(geometry, point)) {
        case FrameHit::Caption: return HTCAPTION;
        case FrameHit::SystemMenu: return HTSYSMENU;
        case FrameHit::Left: return HTLEFT;
        case FrameHit::Right: return HTRIGHT;
        case FrameHit::Top: return HTTOP;
        case FrameHit::Bottom: return HTBOTTOM;
        case FrameHit::TopLeft: return HTTOPLEFT;
        case FrameHit::TopRight: return HTTOPRIGHT;
        case FrameHit::BottomLeft: return HTBOTTOMLEFT;
        case FrameHit::BottomRight: return HTBOTTOMRIGHT;
        case FrameHit::Client: return HTCLIENT;
        }
        return HTCLIENT;
    }

    void command(WPARAM command) const
    {
        // Posting avoids deleting a Qt sender during an accessibility action
        // or re-entering its clicked handler. Qt still handles WM_CLOSE.
        PostMessageW(hwnd, WM_SYSCOMMAND, command, 0);
    }

    bool process(const QByteArray& eventType, void* message, qintptr* result)
    {
        if (!available() || owner.m_mode != midi_play::settings::TitleBarMode::Custom
            || !message || !result || !owner.m_window
            || (eventType != QByteArrayLiteral("windows_generic_MSG")
                && eventType != QByteArrayLiteral("windows_dispatcher_MSG"))) return false;
        const auto& msg = *static_cast<const MSG*>(message);
        const auto handle = reinterpret_cast<HWND>(owner.m_window->internalWinId());
        if (!handle || msg.hwnd != handle) return false;
        bind(handle);
        if (!fullScreen() && owner.m_captionButtons) {
            if (msg.message == WM_NCMOUSEMOVE) {
                POINT point {GET_X_LPARAM(msg.lParam), GET_Y_LPARAM(msg.lParam)};
                owner.m_captionButtons->setNativeHovered(captionButtonAt(point));
                TRACKMOUSEEVENT track {sizeof(TRACKMOUSEEVENT), TME_LEAVE | TME_NONCLIENT, hwnd, 0};
                TrackMouseEvent(&track);
                // Keep DefWindowProc's non-client mouse processing for Snap.
            } else if (msg.message == WM_NCMOUSELEAVE) {
                owner.m_captionButtons->setNativeHovered(nullptr);
            } else if (msg.message == WM_NCLBUTTONDOWN || msg.message == WM_NCLBUTTONDBLCLK) {
                POINT point {GET_X_LPARAM(msg.lParam), GET_Y_LPARAM(msg.lParam)};
                if (auto* button = captionButtonAt(point)) {
                    pressedButton = button;
                    owner.m_captionButtons->setNativePressed(button);
                    SetCapture(hwnd);
                    *result = 0;
                    return true;
                }
            } else if (pressedButton && (msg.message == WM_MOUSEMOVE || msg.message == WM_LBUTTONUP
                                        || msg.message == WM_NCLBUTTONUP)) {
                POINT point {GET_X_LPARAM(msg.lParam), GET_Y_LPARAM(msg.lParam)};
                if (msg.message != WM_NCLBUTTONUP) ClientToScreen(hwnd, &point);
                auto* hovered = captionButtonAt(point);
                owner.m_captionButtons->setNativeHovered(hovered);
                owner.m_captionButtons->setNativePressed(hovered == pressedButton ? hovered : nullptr);
                if (msg.message != WM_MOUSEMOVE) {
                    const QPointer<QToolButton> clicked = hovered == pressedButton ? pressedButton : nullptr;
                    clearCaptionInput();
                    if (clicked) clicked->click();
                }
                *result = 0;
                return true;
            } else if (msg.message == WM_CAPTURECHANGED || msg.message == WM_CANCELMODE) {
                clearCaptionInput();
            }
        }
        switch (msg.message) {
        case WM_NCCALCSIZE:
            *result = calculateClientRect(msg);
            return true;
        case WM_NCHITTEST:
            *result = hitTest(msg);
            return true;
        case WM_DPICHANGED:
            dpi = HIWORD(msg.wParam);
            // Let Qt apply the suggested rectangle and update its screen/DPR.
            owner.scheduleRefresh();
            break;
        case WM_DWMCOMPOSITIONCHANGED:
        case WM_THEMECHANGED:
        case WM_SETTINGCHANGE:
            owner.scheduleRefresh();
            break;
        case WM_DESTROY:
            clearCaptionInput();
            break;
        case WM_NCMOUSEMOVE:
        case WM_NCMOUSELEAVE:
        case WM_NCLBUTTONDOWN:
        case WM_NCLBUTTONUP:
        case WM_NCLBUTTONDBLCLK:
        case WM_NCRBUTTONUP:
        case WM_NCACTIVATE: {
            LRESULT value = 0;
            if (!fullScreen() && DwmDefWindowProc(hwnd, msg.message, msg.wParam, msg.lParam, &value)) {
                *result = value;
                return true;
            }
            break;
        }
        default:
            break;
        }
        // Qt/DefWindowProc retain state, min/max size hints, system menu,
        // closeEvent, keyboard shortcuts, drag loops and monitor placement.
        return false;
    }
#endif
};

WindowChromeController::WindowChromeController(QWidget* window)
    : QObject(window), m_window(window), m_native(std::make_unique<NativeFrame>(*this))
{
    if (window) window->installEventFilter(this);
}

WindowChromeController::~WindowChromeController() = default;

void WindowChromeController::setTitleBar(QWidget* titleBar, QWidget* systemMenuWidget,
                                         const QVector<QWidget*>& interactiveWidgets)
{
    if (m_titleBar) m_titleBar->removeEventFilter(this);
    m_titleBar = titleBar;
    m_systemMenuWidget = systemMenuWidget;
    m_interactiveWidgets.clear();
    for (auto* widget : interactiveWidgets) m_interactiveWidgets.append(widget);
    if (titleBar) titleBar->installEventFilter(this);
    scheduleRefresh();
}

void WindowChromeController::setCaptionButtons(CaptionButtons* buttons)
{
    if (m_captionButtons) disconnect(m_captionButtons, nullptr, this, nullptr);
    m_captionButtons = buttons;
    if (buttons) {
        buttons->setTheme(m_theme);
        connect(buttons, &CaptionButtons::commandRequested, this, [this](CaptionCommand command) {
            switch (command) {
            case CaptionCommand::Minimize: minimize(); break;
            case CaptionCommand::MaximizeOrRestore: maximizeOrRestore(); break;
            case CaptionCommand::Close: close(); break;
            }
        });
    }
    scheduleRefresh();
}

void WindowChromeController::setMode(midi_play::settings::TitleBarMode mode)
{
    const auto normalized = midi_play::settings::normalizeTitleBarMode(mode);
    if (m_mode == normalized) return;
    m_mode = normalized;
    emit modeChanged(m_mode);
    refreshFrame();
}

void WindowChromeController::setTheme(midi_play::settings::ThemeMode theme)
{
    m_theme = midi_play::settings::normalizeThemeMode(theme);
    if (m_captionButtons) m_captionButtons->setTheme(m_theme);
#if defined(Q_OS_WIN)
    if (m_native->customApplied) m_native->applyAppearance();
#endif
}

int WindowChromeController::captionButtonsWidth() const noexcept { return m_native->buttonsWidth; }
int WindowChromeController::minimumCaptionHeight() const noexcept { return m_native->captionHeight; }

void WindowChromeController::scheduleRefresh()
{
    if (m_refreshPending) return;
    m_refreshPending = true;
    QTimer::singleShot(0, this, [this] {
        m_refreshPending = false;
        refreshFrame();
    });
}

void WindowChromeController::refreshFrame()
{
#if defined(Q_OS_WIN)
    if (m_native->available() && m_window) {
        // Native mode never forces early HWND creation or modifies its theme.
        if (m_mode == midi_play::settings::TitleBarMode::Native && !m_native->customApplied) return;
        const QString error = m_native->apply();
        if (!error.isEmpty()) {
            m_mode = midi_play::settings::TitleBarMode::Native;
            m_native->restoreAttributes();
            m_native->apply();
            emit modeChanged(m_mode);
            emit nativeIntegrationFailed(error);
        }
    }
#endif
}

bool WindowChromeController::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_window) {
        switch (event->type()) {
        case QEvent::WinIdChange:
        case QEvent::Show:
        case QEvent::WindowStateChange:
        case QEvent::ScreenChangeInternal:
        case QEvent::DevicePixelRatioChange:
            scheduleRefresh();
            break;
        case QEvent::ActivationChange:
            if (m_captionButtons) m_captionButtons->setWindowActive(m_window->isActiveWindow());
            break;
        default: break;
        }
    } else if (watched == m_titleBar && event->type() == QEvent::Resize) {
        const auto* resize = static_cast<QResizeEvent*>(event);
        if (resize->size().height() != resize->oldSize().height()) scheduleRefresh();
    }
    return QObject::eventFilter(watched, event);
}

bool WindowChromeController::processNativeEvent(const QByteArray& eventType, void* message, qintptr* result)
{
#if defined(Q_OS_WIN)
    return m_native->process(eventType, message, result);
#else
    Q_UNUSED(eventType)
    Q_UNUSED(message)
    Q_UNUSED(result)
    return false;
#endif
}

void WindowChromeController::minimize()
{
#if defined(Q_OS_WIN)
    if (m_native->available() && m_native->hwnd) { m_native->command(SC_MINIMIZE); return; }
#endif
    if (m_window) m_window->showMinimized();
}

void WindowChromeController::maximizeOrRestore()
{
    if (!m_window || m_window->isFullScreen()) return;
#if defined(Q_OS_WIN)
    if (m_native->available() && m_native->hwnd) {
        m_native->command(IsZoomed(m_native->hwnd) ? SC_RESTORE : SC_MAXIMIZE);
        return;
    }
#endif
    if (m_window->isMaximized()) m_window->showNormal();
    else m_window->showMaximized();
}

void WindowChromeController::close()
{
#if defined(Q_OS_WIN)
    if (m_native->available() && m_native->hwnd) { m_native->command(SC_CLOSE); return; }
#endif
    if (m_window) m_window->close();
}

void WindowChromeController::showSystemMenu()
{
#if defined(Q_OS_WIN)
    if (!m_native->available() || !m_native->hwnd || !m_window || m_window->isFullScreen()) return;
    const HWND hwnd = m_native->hwnd;
    const HMENU menu = GetSystemMenu(hwnd, FALSE);
    if (!menu) return;
    SendMessageW(hwnd, WM_INITMENU, reinterpret_cast<WPARAM>(menu), 0);
    RECT window {};
    GetWindowRect(hwnd, &window);
    const QRectF icon = m_native->widgetRect(m_systemMenuWidget);
    const int x = window.left + qRound(icon.left());
    const int y = window.top + qRound(icon.isEmpty() ? m_native->frameY() : icon.bottom());
    const UINT command = TrackPopupMenuEx(menu, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_RIGHTBUTTON,
                                         x, y, hwnd, nullptr);
    if (command) m_native->command(command);
#endif
}

} // namespace midi_play::presentation::windowchrome
