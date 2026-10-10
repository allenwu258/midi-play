// 原生窗口装饰实现：保留系统窗口能力，将自定义标题内容扩展到客户区并桥接按钮输入。
#include "windowchromecontroller.h"

#include "windowchromegeometry.h"
#include "captionbuttons.h"
#include "presentation/theme/apptheme.h"

#include <QEvent>
#include <QGuiApplication>
#include <QPalette>
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

// 控制器私有状态：原生修改只在 Windows Qt 平台插件下执行。
struct WindowChromeController::NativeFrame {
    explicit NativeFrame(WindowChromeController& controller) : owner(controller) {}

    WindowChromeController& owner;
    int buttonsWidth = 0;
    int captionHeight = 0;

#if defined(Q_OS_WIN)
    // 保存可读取的原始 DWM 属性，退出自定义模式时逐项恢复。
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
    QPointer<QToolButton> pressedButton; // 弱引用跨越一次原生按压/释放，控件销毁后自动失效。

    // 用屏幕物理坐标查找可见且启用的系统按钮，返回用于桥接的 Qt 控件。
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

    // 清除悬停、按下与捕获状态，取消或切换窗口句柄时均可调用。
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
        // 离屏平台的 WId 是不透明标识，不能作为 HWND 传入 Win32。
        return QGuiApplication::platformName() == QLatin1String("windows");
    }

    // 优先使用 Qt 当前 DPR，窗口句柄未就绪时按 Windows DPI 回退计算。
    qreal scale() const noexcept
    {
        const auto* window = owner.m_window ? owner.m_window->windowHandle() : nullptr;
        return window ? window->devicePixelRatio() : qreal(dpi) / USER_DEFAULT_SCREEN_DPI;
    }

    // 缩放边框包含系统尺寸框架和附加边框，均以物理像素计。
    int frameX() const { return GetSystemMetricsForDpi(SM_CXSIZEFRAME, dpi)
                            + GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi); }
    int frameY() const { return GetSystemMetricsForDpi(SM_CYSIZEFRAME, dpi)
                            + GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi); }

    // 新句柄重新建立原生状态，旧句柄的属性快照不可套用到新窗口。
    void bind(HWND handle)
    {
        if (hwnd == handle) return;
        clearCaptionInput();
        hwnd = handle;
        customApplied = false;
        for (auto& attribute : attributes) attribute.captured = false;
    }

    // 只记录成功读取的属性，兼容不支持部分新 DWM 属性的系统版本。
    void captureAttributes()
    {
        for (auto& attribute : attributes) {
            attribute.captured = SUCCEEDED(DwmGetWindowAttribute(
                hwnd, attribute.id, &attribute.value, sizeof(attribute.value)));
        }
    }

    // 动态属性或系统配色变化后重新求值顶栏及子控件的样式选择器。
    void refreshTitleBarStyle()
    {
        if (!owner.m_titleBar) return;
        owner.m_titleBar->style()->unpolish(owner.m_titleBar);
        owner.m_titleBar->style()->polish(owner.m_titleBar);
        for (auto* widget : owner.m_titleBar->findChildren<QWidget*>()) {
            widget->style()->unpolish(widget);
            widget->style()->polish(widget);
            widget->update();
        }
        owner.m_titleBar->update();
    }

    // 移除系统高对比度覆盖，恢复应用主题继承关系。
    void resetQtAppearance()
    {
        if (owner.m_captionButtons) {
            owner.m_captionButtons->setSystemColors(false, {}, {}, {}, {});
        }
        if (!owner.m_titleBar) return;

        const bool hadHighContrast = owner.m_titleBar->property("nativeHighContrast").isValid();
        owner.m_titleBar->setProperty("nativeHighContrast", QVariant());
        // 空调色板清除控件局部覆盖，恢复从窗口或应用继承的颜色。
        owner.m_titleBar->setPalette(QPalette());
        if (hadHighContrast) refreshTitleBarStyle();
    }

    // 清理输入和 Qt 外观，再撤销客户区扩展及成功捕获的 DWM 属性修改。
    void restoreAttributes()
    {
        clearCaptionInput();
        resetQtAppearance();
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

    // 同步 DWM 与 Qt 标题栏配色；系统高对比度设置优先于应用深浅主题。
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
            if (highContrast) {
                QPalette palette = owner.m_window->palette();
                const auto systemColor = [](int index) {
                    const COLORREF color = GetSysColor(index);
                    return QColor(GetRValue(color), GetGValue(color), GetBValue(color));
                };
                palette.setColor(QPalette::Window, systemColor(COLOR_WINDOW));
                palette.setColor(QPalette::WindowText, systemColor(COLOR_WINDOWTEXT));
                palette.setColor(QPalette::ButtonText, systemColor(COLOR_BTNTEXT));
                palette.setColor(QPalette::Highlight, systemColor(COLOR_HIGHLIGHT));
                palette.setColor(QPalette::HighlightedText, systemColor(COLOR_HIGHLIGHTTEXT));
                owner.m_titleBar->setPalette(palette);
            } else {
                owner.m_titleBar->setPalette(QPalette());
            }
            if (owner.m_titleBar->property("nativeHighContrast").toBool() != highContrast) {
                if (highContrast) owner.m_titleBar->setProperty("nativeHighContrast", true);
                else owner.m_titleBar->setProperty("nativeHighContrast", QVariant());
                refreshTitleBarStyle();
            }
        }
        // 高对比度时使用系统颜色和默认 DWM 色值，普通模式才应用主题色。
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
        // Windows 10 对不支持的 Windows 11 属性返回 E_INVALIDARG，基础边框能力仍可用。
    }

    // 应用模式并刷新非客户区，空字符串表示成功或当前平台无需处理。
    QString apply()
    {
        if (!available() || !owner.m_window) return {};
        bind(reinterpret_cast<HWND>(owner.m_window->winId()));
        const UINT currentDpi = GetDpiForWindow(hwnd);
        if (currentDpi) dpi = currentDpi;

        const bool custom = owner.m_mode == midi_play::settings::TitleBarMode::Custom;
        if (custom) {
            if (fullScreen()) clearCaptionInput();
            // 自定义模式依赖 DWM 和原生窗口样式；全屏不要求可见标题及厚边框。
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

        // 保持句柄、激活、层级和窗口位置，仅刷新框架；Qt 6.8 会读取 NCCALCSIZE 的结果边距。
        if (!SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
                          SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE
                          | SWP_NOOWNERZORDER | SWP_FRAMECHANGED)) {
            return QStringLiteral("无法刷新 Windows 窗口框架（%1），已使用原生标题栏")
                .arg(GetLastError());
        }

        if (custom) {
            // 扩展高度包括实际顶栏和顶部系统边框，全屏时扩展边距归零。
            const int height = owner.m_titleBar
                ? qCeil(owner.m_titleBar->height() * scale()) + frameY() : frameY();
            const MARGINS margins {0, 0, fullScreen() ? 0 : height, 0};
            if (FAILED(DwmExtendFrameIntoClientArea(hwnd, &margins)))
                return QStringLiteral("无法扩展 DWM 标题区域，已使用原生标题栏");
        }
        updateMetrics();
        return {};
    }

    // 将系统按钮区域换算为 Qt 逻辑尺寸，DWM 无有效度量时回退系统尺寸。
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
                // DWM 按钮区域相对窗口，Qt 内容相对真实客户区原点；最大化前后原点偏移不同。
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

    // 最大化时为当前显示器的自动隐藏任务栏保留边缘唤出区域。
    void leaveAutoHideTaskbarRevealArea(RECT& rect) const
    {
        MONITORINFO monitor {sizeof(MONITORINFO)};
        if (!GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &monitor)) return;
        for (UINT edge : {ABE_LEFT, ABE_TOP, ABE_RIGHT, ABE_BOTTOM}) {
            APPBARDATA bar {sizeof(APPBARDATA)};
            bar.uEdge = edge;
            bar.rc = monitor.rcMonitor;
            if (!SHAppBarMessage(ABM_GETAUTOHIDEBAREX, &bar)) continue;
            // 工作区已排除的边缘无需重复扣除；其余边缘预留 2 个物理像素。
            constexpr LONG revealPixels = 2;
            if (edge == ABE_LEFT && rect.left <= monitor.rcMonitor.left) rect.left += revealPixels;
            if (edge == ABE_TOP && rect.top <= monitor.rcMonitor.top) rect.top += revealPixels;
            if (edge == ABE_RIGHT && rect.right >= monitor.rcMonitor.right) rect.right -= revealPixels;
            if (edge == ABE_BOTTOM && rect.bottom >= monitor.rcMonitor.bottom) rect.bottom -= revealPixels;
        }
    }

    // 处理 WM_NCCALCSIZE 的两种参数布局，将标题内容纳入客户区。
    LRESULT calculateClientRect(const MSG& message) const
    {
        RECT& client = message.wParam
            ? reinterpret_cast<NCCALCSIZE_PARAMS*>(message.lParam)->rgrc[0]
            : *reinterpret_cast<RECT*>(message.lParam);
        const RECT proposed = client;
        if (fullScreen()) return 0;

        // 保留系统左、右、下边框计算，只重设顶部；最大化时扣除超出工作区的厚边框。
        DefWindowProcW(hwnd, message.message, message.wParam, message.lParam);
        UINT visibleBorder = std::max(1, MulDiv(1, dpi, USER_DEFAULT_SCREEN_DPI));
        DwmGetWindowAttribute(hwnd, DWMWA_VISIBLE_FRAME_BORDER_THICKNESS,
                              &visibleBorder, sizeof(visibleBorder));
        client.top = proposed.top + (IsZoomed(hwnd) ? frameY() : LONG(visibleBorder));
        if (IsZoomed(hwnd)) leaveAutoHideTaskbarRevealArea(client);
        return 0;
    }

    // 将 Qt 控件的逻辑坐标换算为窗口相对物理坐标，包含真实客户区原点偏移。
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

    // 先识别系统按钮以支持原生行为，再用通用几何模型检测拖动与缩放区域。
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
            // 禁用操作控件仍属于客户区，不能变成可拖动的标题区域。
            const QRectF region = widgetRect(widget);
            if (!region.isEmpty()) geometry.interactive.append(region);
        }
        // 整个按钮容器排除拖动；禁用系统按钮不会获得原生命中类型或触发系统命令。
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
        // 异步投递避免在无障碍动作或 clicked 回调中销毁发送者；关闭仍经过 Qt 的 WM_CLOSE 处理。
        PostMessageW(hwnd, WM_SYSCOMMAND, command, 0);
    }

    // 只接管自定义模式下属于本窗口的 Win32 消息，其余交回 Qt 与默认窗口过程。
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
        // 非客户区输入同步到 Qt 按钮状态；捕获后客户区消息也参与同一次按下/释放事务。
        if (!fullScreen() && owner.m_captionButtons) {
            if (msg.message == WM_NCMOUSEMOVE) {
                POINT point {GET_X_LPARAM(msg.lParam), GET_Y_LPARAM(msg.lParam)};
                owner.m_captionButtons->setNativeHovered(captionButtonAt(point));
                TRACKMOUSEEVENT track {sizeof(TRACKMOUSEEVENT), TME_LEAVE | TME_NONCLIENT, hwnd, 0};
                TrackMouseEvent(&track);
                // 保留默认非客户区鼠标处理，以支持系统贴靠布局。
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
                    // 仅在原按钮内释放时触发 click；先清理捕获，再执行可能改变窗口状态的命令。
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
            // 建议窗口矩形与屏幕/DPR 更新由 Qt 处理，随后再刷新框架度量。
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
        // 未接管的消息保留 Qt/系统对状态、尺寸约束、菜单、关闭、快捷键、拖动和显示器位置的处理。
        return false;
    }
#endif
};

// 由被装饰窗口管理生命周期，事件过滤器仅观察需要更新框架的 Qt 事件。
WindowChromeController::WindowChromeController(QWidget* window)
    : QObject(window), m_window(window), m_native(std::make_unique<NativeFrame>(*this))
{
    if (window) window->installEventFilter(this);
}

WindowChromeController::~WindowChromeController() = default;

// 替换标题栏命中区域及交互控件列表；借用对象通过弱指针跟踪。
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

// 断开旧按钮组并绑定新命令入口，避免更换控件后重复执行窗口操作。
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

// 合并同一事件循环内的 DPI、句柄及尺寸变化，待 Qt 状态更新后刷新一次。
void WindowChromeController::scheduleRefresh()
{
    if (m_refreshPending) return;
    m_refreshPending = true;
    QTimer::singleShot(0, this, [this] {
        m_refreshPending = false;
        refreshFrame();
    });
}

// 应用当前框架模式；原生集成失败时恢复属性并发布最终回退模式。
void WindowChromeController::refreshFrame()
{
#if defined(Q_OS_WIN)
    if (m_native->available() && m_window) {
        // 尚未应用自定义框架的原生模式直接返回，避免提前创建 HWND 或覆盖系统主题。
        if (m_mode == midi_play::settings::TitleBarMode::Native && !m_native->customApplied) return;
        const QString error = m_native->apply();
        if (!error.isEmpty()) {
            // 恢复捕获的外观与框架，再公布回退结果，主窗口据此同步持久化模式。
            m_mode = midi_play::settings::TitleBarMode::Native;
            m_native->restoreAttributes();
            m_native->apply();
            emit modeChanged(m_mode);
            emit nativeIntegrationFailed(error);
        }
    }
#endif
}

// 观察窗口/标题栏变化但不消费事件，原有 Qt 激活与布局流程继续执行。
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

// Windows 消息由私有实现按窗口及模式筛选；其他平台返回未处理。
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

// 在系统图标下方弹出原生菜单，使用屏幕物理坐标并异步投递所选命令。
void WindowChromeController::showSystemMenu()
{
#if defined(Q_OS_WIN)
    if (!m_native->available() || !m_native->hwnd || !m_window || m_window->isFullScreen()) return;
    const HWND hwnd = m_native->hwnd;
    const HMENU menu = GetSystemMenu(hwnd, FALSE);
    if (!menu) return;
    // 先让系统更新菜单项状态，再在图标下方显示；所选命令继续异步投递。
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
