#include "presentation/windowchrome/windowchromecontroller.h"
#include "presentation/windowchrome/windowchromegeometry.h"
#include "presentation/windowchrome/captionbuttons.h"
#include "app/playerapplicationservice.h"
#include "app/settingsservice.h"
#include "infrastructure/settings/qsettingsstore.h"
#include "presentation/mainwindow.h"
#include "presentation/visualization/fallingnotesview.h"
#include "domain/visualization/playbackvisualizationprojector.h"
#if MIDI_PLAY_HAS_VULKAN
#include "presentation/visualization/fallingnotesvulkanwindow.h"
#endif

#include <QApplication>
#include <QCloseEvent>
#include <QElapsedTimer>
#include <QHBoxLayout>
#include <QLabel>
#include <QThread>
#include <QTemporaryDir>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWindow>

#if defined(Q_OS_WIN)
#include <windows.h>
#include <windowsx.h>
#include <dwmapi.h>
#endif

#include <cstdio>
#include <cstdlib>
#include <functional>

namespace {
using namespace midi_play::presentation::windowchrome;
using midi_play::settings::TitleBarMode;
using midi_play::settings::ThemeMode;

void require(bool condition, const char* message)
{
    if (!condition) {
        std::fprintf(stderr, "FAILED: %s\n", message);
        std::exit(EXIT_FAILURE);
    }
}

void testGeometry()
{
    for (qreal scale : {1.0, 1.25, 1.5, 1.75, 2.0, 2.5}) {
        FrameGeometry g;
        g.window = QRectF(0, 0, 800 * scale, 500 * scale);
        g.caption = QRectF(0, 0, 800 * scale, 58 * scale);
        g.systemMenu = QRectF(16 * scale, 15 * scale, 24 * scale, 28 * scale);
        g.interactive = {QRectF(500 * scale, 10 * scale, 100 * scale, 36 * scale)};
        g.resizeBorder = QMargins(8 * scale, 8 * scale, 8 * scale, 8 * scale);
        const auto at = [&](qreal x, qreal y) { return hitTestFrame(g, QPointF(x * scale, y * scale)); };
        require(at(1, 1) == FrameHit::TopLeft && at(799, 1) == FrameHit::TopRight,
                "top corners must remain resizable at every DPI");
        require(at(1, 499) == FrameHit::BottomLeft && at(799, 499) == FrameHit::BottomRight,
                "bottom corners must remain resizable at every DPI");
        require(at(400, 1) == FrameHit::Top && at(400, 499) == FrameHit::Bottom,
                "top/bottom resize borders must precede caption regions");
        require(at(1, 250) == FrameHit::Left && at(799, 250) == FrameHit::Right,
                "left/right resize borders must be symmetric");
        require(at(400, 30) == FrameHit::Caption && at(400, 100) == FrameHit::Client,
                "only the title region is draggable");
        require(at(550, 30) == FrameHit::Client && at(25, 30) == FrameHit::SystemMenu,
                "interactive and system-icon areas must exclude caption dragging");
        g.maximized = true;
        require(at(400, 1) == FrameHit::Caption && at(1, 250) == FrameHit::Client,
                "maximized windows must not expose resize borders");
        g.fullScreen = true;
        require(at(25, 30) == FrameHit::Client && at(400, 30) == FrameHit::Client,
                "full screen must disable all caption semantics");
        g.fullScreen = false;
        g.maximized = false;
        g.resizeHorizontal = false;
        require(at(1, 250) == FrameHit::Client && at(1, 1) == FrameHit::Top,
                "fixed-width windows must only offer vertical resizing");
        g.resizeVertical = false;
        require(at(400, 1) == FrameHit::Caption && at(799, 499) == FrameHit::Client,
                "fixed-size windows must not offer resize cursors");
    }

    FrameGeometry negative;
    negative.window = QRectF(-1920, -300, 800, 500);
    negative.caption = QRectF(-1920, -300, 800, 58);
    negative.resizeBorder = QMargins(8, 8, 8, 8);
    require(hitTestFrame(negative, QPointF(-1919, -299)) == FrameHit::TopLeft,
            "negative monitor coordinates must preserve signed hit tests");
}

#if defined(Q_OS_WIN)
void waitFor(const std::function<bool()>& condition, const char* message)
{
    QElapsedTimer timer;
    timer.start();
    do {
        QApplication::processEvents();
        if (condition()) return;
        QThread::msleep(5);
    } while (timer.elapsed() < 5000);
    require(false, message);
}

class ChromeWindow final : public QWidget {
public:
    ChromeWindow()
    {
        resize(860, 500);
        setMinimumSize(300, 200);
        setWindowTitle(QStringLiteral("MIDI Play — native chrome integration test"));
        chrome = new WindowChromeController(this);
        auto* root = new QVBoxLayout(this);
        root->setContentsMargins(0, 0, 0, 0);
        title = new QWidget(this);
        title->setFixedHeight(58);
        titleLayout = new QHBoxLayout(title);
        titleLayout->setContentsMargins(16, 0, 12, 0);
        icon = new QToolButton(title);
        icon->setFixedSize(24, 28);
        icon->setText(QStringLiteral("M"));
        titleLayout->addWidget(icon);
        titleLayout->addWidget(new QLabel(QStringLiteral("Caption"), title));
        titleLayout->addStretch();
        // A nested, disabled interactive control must remain HTCLIENT.
        auto* nested = new QWidget(title);
        auto* nestedLayout = new QHBoxLayout(nested);
        nestedLayout->setContentsMargins(0, 0, 0, 0);
        action = new QToolButton(nested);
        action->setText(QStringLiteral("Action"));
        action->setFixedSize(90, 36);
        action->setEnabled(false);
        nestedLayout->addWidget(action);
        titleLayout->addWidget(nested);
        buttons = new CaptionButtons(title);
        buttons->hide();
        titleLayout->addWidget(buttons, 0, Qt::AlignTop);
        root->addWidget(title);
        child = new QWindow;
        child->setSurfaceType(QSurface::RasterSurface);
        root->addWidget(QWidget::createWindowContainer(child, this), 1);
        chrome->setTitleBar(title, icon, {action});
        chrome->setCaptionButtons(buttons);
        QObject::connect(chrome, &WindowChromeController::frameMetricsChanged, this, [this] {
            buttons->setVisible(chrome->mode() == TitleBarMode::Custom && !isFullScreen());
        });
        QObject::connect(chrome, &WindowChromeController::modeChanged, this, [this] {
            buttons->setVisible(chrome->mode() == TitleBarMode::Custom && !isFullScreen());
        });
    }

    WindowChromeController* chrome = nullptr;
    QWidget* title = nullptr;
    QHBoxLayout* titleLayout = nullptr;
    QToolButton* icon = nullptr;
    QToolButton* action = nullptr;
    CaptionButtons* buttons = nullptr;
    QWindow* child = nullptr;
    int closes = 0;
    bool allowClose = false;

protected:
    bool nativeEvent(const QByteArray& type, void* message, qintptr* result) override
    {
        if (chrome && chrome->processNativeEvent(type, message, result)) return true;
        return QWidget::nativeEvent(type, message, result);
    }
    void closeEvent(QCloseEvent* event) override
    {
        ++closes;
        if (allowClose) event->accept(); else event->ignore();
    }
};

LPARAM hitCoordinates(HWND hwnd, QPoint logicalPoint, qreal scale)
{
    POINT point {qRound(logicalPoint.x() * scale), qRound(logicalPoint.y() * scale)};
    ClientToScreen(hwnd, &point);
    return MAKELPARAM(point.x, point.y);
}

void testNativeFrame()
{
    ChromeWindow window;
    window.show();
    const auto hwnd = reinterpret_cast<HWND>(window.winId());
    waitFor([&] { return IsWindowVisible(hwnd) && window.child->isExposed(); },
            "test window and native child must be visible");
    const WId childId = window.child->winId();
    const auto flags = window.windowFlags();
    DWORD originalDark = 0;
    const bool canReadDark = SUCCEEDED(DwmGetWindowAttribute(
        hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &originalDark, sizeof(originalDark)));

    window.chrome->setMode(TitleBarMode::Custom);
    waitFor([&] { return window.chrome->captionButtonsWidth() > 0; },
            "native caption buttons must reserve Qt content space");
    require(window.chrome->mode() == TitleBarMode::Custom, "DWM integration must remain enabled");
    require(reinterpret_cast<HWND>(window.winId()) == hwnd && window.child->winId() == childId,
            "changing chrome must preserve top-level and native child HWNDs");
    require(window.windowFlags() == flags && !(flags & Qt::FramelessWindowHint),
            "custom chrome must preserve Qt flags and the native frame");
    const LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_STYLE);
    require((style & (WS_CAPTION | WS_THICKFRAME | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX))
            == (WS_CAPTION | WS_THICKFRAME | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX),
            "native caption, resizing, menu and window controls must remain present");

    const auto hit = [&](QPoint point) {
        return SendMessageW(hwnd, WM_NCHITTEST, 0, hitCoordinates(hwnd, point, window.devicePixelRatioF()));
    };
    require(hit(window.title->mapTo(&window, QPoint(250, 30))) == HTCAPTION,
            "blank title content must use native caption semantics");
    require(hit(window.action->mapTo(&window, window.action->rect().center())) == HTCLIENT,
            "nested disabled Qt buttons must not drag the window");
    require(hit(window.icon->mapTo(&window, window.icon->rect().center())) == HTSYSMENU,
            "custom application icon must use the native system menu");
    RECT frame {};
    GetWindowRect(hwnd, &frame);
    require(SendMessageW(hwnd, WM_NCHITTEST, 0, MAKELPARAM(frame.left + 1, frame.top + 1)) == HTTOPLEFT,
            "native frame corner must be resizable");

    auto* maximize = window.buttons->button(CaptionCommand::MaximizeOrRestore);
    const auto buttonCenter = [&](CaptionCommand command) {
        auto* button = window.buttons->button(command);
        return button->mapTo(&window, button->rect().center());
    };
    require(hit(buttonCenter(CaptionCommand::Minimize)) == HTMINBUTTON, "minimize must expose its native hit code");
    require(hit(buttonCenter(CaptionCommand::MaximizeOrRestore)) == HTMAXBUTTON,
            "maximize must expose the Windows 11 Snap entry point");
    require(hit(buttonCenter(CaptionCommand::Close)) == HTCLOSE, "close must expose its native hit code");
    maximize->setEnabled(false);
    require(hit(buttonCenter(CaptionCommand::MaximizeOrRestore)) == HTCLIENT,
            "disabled caption buttons must never emit a native command");
    maximize->setEnabled(true);

    const LPARAM maximizePoint = hitCoordinates(hwnd, buttonCenter(CaptionCommand::MaximizeOrRestore), window.devicePixelRatioF());
    SendMessageW(hwnd, WM_NCLBUTTONDOWN, HTMAXBUTTON, maximizePoint);
    require(GetCapture() == hwnd && maximize->isDown(), "caption press must capture the mouse and update Qt state");
    SendMessageW(hwnd, WM_LBUTTONUP, 0, MAKELPARAM(300, 180));
    QApplication::processEvents();
    require(!IsZoomed(hwnd) && GetCapture() != hwnd && !maximize->isDown(),
            "release outside the pressed button must cancel the command");
    SendMessageW(hwnd, WM_NCLBUTTONDOWN, HTMAXBUTTON, maximizePoint);
    SendMessageW(hwnd, WM_CANCELMODE, 0, 0);
    require(GetCapture() != hwnd && !maximize->isDown(), "capture cancellation must clear caption interaction");
    SendMessageW(hwnd, WM_NCLBUTTONDOWN, HTMAXBUTTON, maximizePoint);
    SendMessageW(hwnd, WM_NCLBUTTONUP, HTMAXBUTTON, maximizePoint);
    waitFor([&] { return IsZoomed(hwnd) && window.isMaximized(); },
            "release inside a caption button must execute exactly one system command");
    window.chrome->maximizeOrRestore();
    waitFor([&] { return !IsZoomed(hwnd); }, "mouse caption restore must complete");

    // UI Automation / keyboard Invoke follows the same system-command path.
    maximize->click();
    waitFor([&] { return IsZoomed(hwnd) && window.isMaximized(); }, "caption Invoke must maximize once");
    window.chrome->maximizeOrRestore();
    waitFor([&] { return !IsZoomed(hwnd); }, "caption Invoke restore must complete");

    SendMessageW(hwnd, WM_NCLBUTTONDBLCLK, HTCAPTION,
                 hitCoordinates(hwnd, QPoint(250, 30), window.devicePixelRatioF()));
    waitFor([&] { return IsZoomed(hwnd) && window.isMaximized(); },
            "native double click must maximize and synchronize Qt state");
    RECT client {};
    POINT origin {};
    MONITORINFO monitor {sizeof(MONITORINFO)};
    GetClientRect(hwnd, &client);
    ClientToScreen(hwnd, &origin);
    GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &monitor);
    require(origin.x >= monitor.rcWork.left && origin.y >= monitor.rcWork.top
            && origin.x + client.right <= monitor.rcWork.right
            && origin.y + client.bottom <= monitor.rcWork.bottom,
            "maximized client content must remain inside the monitor work area");
    window.chrome->maximizeOrRestore();
    waitFor([&] { return !IsZoomed(hwnd) && !window.isMaximized(); }, "restore must synchronize Qt state");

    window.chrome->minimize();
    waitFor([&] { return IsIconic(hwnd) && window.isMinimized(); }, "minimize must synchronize Qt state");
    window.chrome->setMode(TitleBarMode::Native);
    require(IsIconic(hwnd), "mode changes must preserve minimized state");
    window.chrome->setMode(TitleBarMode::Custom);
    PostMessageW(hwnd, WM_SYSCOMMAND, SC_RESTORE, 0);
    waitFor([&] { return !IsIconic(hwnd) && window.isVisible(); }, "taskbar restore command must work");

    window.showFullScreen();
    waitFor([&] { return window.isFullScreen() && window.chrome->captionButtonsWidth() == 0; },
            "full screen must remove the system-button reservation");
    require(hit(QPoint(250, 30)) == HTCLIENT, "full screen must suppress caption hit testing");
    window.showNormal();
    waitFor([&] { return !window.isFullScreen() && window.chrome->captionButtonsWidth() > 0; },
            "leaving full screen must restore DWM chrome");

    window.chrome->setTheme(ThemeMode::Light);
    DWORD dark = 1;
    if (canReadDark) {
        DwmGetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
        require(dark == 0, "light theme must update native DWM appearance");
    }
    window.chrome->setMode(TitleBarMode::Native);
    QApplication::processEvents();
    GetWindowRect(hwnd, &frame);
    GetClientRect(hwnd, &client);
    ClientToScreen(hwnd, &origin);
    require(origin.y - frame.top > 10 && window.chrome->captionButtonsWidth() == 0,
            "Native mode must restore the real caption and remove content reservation");
    if (canReadDark) {
        DwmGetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
        require(dark == originalDark, "Native mode must restore pre-existing DWM attributes");
    }
    require(window.child->winId() == childId && reinterpret_cast<HWND>(window.winId()) == hwnd,
            "all state and mode round trips must retain native handles");

    window.chrome->setMode(TitleBarMode::Custom);
    window.chrome->close();
    waitFor([&] { return window.closes == 1; }, "system close must reach Qt closeEvent");
    require(window.isVisible(), "Qt closeEvent must be able to reject system close");
    window.allowClose = true;
    window.chrome->close();
    waitFor([&] { return !window.isVisible(); }, "accepted system close must hide the test window");
}

bool testMainWindow(bool useVulkan)
{
    using midi_play::settings::GraphicsMode;
    using midi_play::presentation::visualization::FallingNotesView;
    QTemporaryDir directory;
    require(directory.isValid(), "integration settings must be isolated from user preferences");
    midi_play::app::SettingsService settings(std::make_unique<
        midi_play::infrastructure::settings::QSettingsStore>(directory.filePath(QStringLiteral("settings.ini"))));
    settings.load();
    settings.setGraphicsMode(useVulkan ? GraphicsMode::VulkanExperimental : GraphicsMode::Traditional);
    midi_play::app::PlayerApplicationService player;
    midi_play::presentation::MainWindow window(&player, &settings);
    auto* view = window.findChild<FallingNotesView*>();
    auto* chrome = window.findChild<WindowChromeController*>();
    auto* buttons = window.findChild<CaptionButtons*>();
    require(view && chrome && buttons, "MainWindow must expose its composed chrome and renderer");
    midi_play::music::MusicDocument document;
    document.tempos().push_back({0, 120.0, 0});
    document.setDuration(1920);
    midi_play::music::Track track;
    track.id = QStringLiteral("piano");
    midi_play::music::NoteEvent note;
    note.noteId = 1;
    note.start = 480;
    note.duration = 480;
    note.pitch = 60;
    track.notes.push_back(note);
    document.tracks().push_back(track);
    document.rebuildMeasureGrid();
    const auto chart = midi_play::visualization::PlaybackVisualizationProjector().project(document, 1);
    require(bool(chart), "renderer integration must have a loaded chart");
    view->setChart(chart);
    view->setTransportPosition(650'000, chart->durationUs());
    view->setTransportState(midi_play::playback::State::Paused);
    view->setPlaybackRate(150);
    window.show();
    const auto hwnd = reinterpret_cast<HWND>(window.winId());
    int renderedFrames = 0;
#if MIDI_PLAY_HAS_VULKAN
    QPointer<midi_play::presentation::visualization::FallingNotesVulkanWindow> vulkan;
    WId rendererId = 0;
    VkDevice device = VK_NULL_HANDLE;
    if (useVulkan) {
        for (auto* candidate : QGuiApplication::allWindows()) {
            if (auto* renderer = qobject_cast<midi_play::presentation::visualization::FallingNotesVulkanWindow*>(candidate))
                vulkan = renderer;
        }
        if (!vulkan) { std::puts("SKIPPED: Vulkan renderer is unavailable"); return false; }
        bool initializationFailed = false;
        QObject::connect(vulkan, &midi_play::presentation::visualization::FallingNotesVulkanWindow::frameRendered,
                         &window, [&] { ++renderedFrames; });
        QObject::connect(vulkan, &midi_play::presentation::visualization::FallingNotesVulkanWindow::initializationFailed,
                         &window, [&](const QString& message) {
            initializationFailed = true;
            std::fprintf(stderr, "SKIPPED: Vulkan initialization failed: %s\n", qPrintable(message));
        });
        QElapsedTimer vulkanTimer;
        vulkanTimer.start();
        while (!initializationFailed && renderedFrames < 2 && vulkanTimer.elapsed() < 5000) {
            QApplication::processEvents();
            QThread::msleep(10);
        }
        if (initializationFailed || renderedFrames < 2) {
            std::puts("SKIPPED: Vulkan produced no initial frame in the desktop test environment");
            return false;
        }
        if (!vulkan) { std::puts("SKIPPED: Vulkan initialization fell back to Qt"); return false; }
        rendererId = vulkan->winId();
        device = vulkan->device();
        require(device != VK_NULL_HANDLE, "Vulkan test must hold a live GPU device");
    }
#else
    if (useVulkan) return false;
#endif
    for (int cycle = 0; cycle < 3; ++cycle) {
        settings.setTitleBarMode(TitleBarMode::Custom);
        waitFor([&] { return chrome->mode() == TitleBarMode::Custom && buttons->isVisible(); },
                "MainWindow must apply the persisted Custom setting immediately");
        require(reinterpret_cast<HWND>(window.winId()) == hwnd, "MainWindow mode changes must retain HWND");
        window.showMaximized();
        waitFor([&] { return IsZoomed(hwnd); }, "MainWindow must maximize with custom chrome");
        settings.setTitleBarMode(TitleBarMode::Native);
        require(IsZoomed(hwnd) && !buttons->isVisible(), "mode changes must retain maximized state");
        window.showNormal();
        settings.setThemeMode(cycle % 2 ? ThemeMode::Dark : ThemeMode::Light);
        QApplication::processEvents();
        const auto state = view->exportState();
        require(state.chart == chart && state.transportPositionUs == 650'000
                && state.transportState == midi_play::playback::State::Paused,
                "chrome changes must preserve chart and visualization transport");
#if MIDI_PLAY_HAS_VULKAN
        if (useVulkan) {
            const int previousFrames = renderedFrames;
            waitFor([&] { return renderedFrames > previousFrames; },
                    "Vulkan must keep rendering after chrome mode and geometry changes");
            require(vulkan && vulkan->winId() == rendererId && vulkan->device() == device,
                    "chrome must retain the Vulkan window and device");
        }
#endif
    }
    window.close();
    return true;
}
#endif

} // namespace

int main(int argc, char** argv)
{
    testGeometry();
    bool native = false;
    bool vulkan = false;
    for (int i = 1; i < argc; ++i) {
        native |= QString::fromLocal8Bit(argv[i]) == QLatin1String("--native");
        vulkan |= QString::fromLocal8Bit(argv[i]) == QLatin1String("--vulkan");
    }
    if (native) {
        QApplication app(argc, argv);
        app.setQuitOnLastWindowClosed(false);
#if defined(Q_OS_WIN)
        if (QGuiApplication::platformName() != QLatin1String("windows")) {
            std::fprintf(stderr, "SKIPPED: a real Qt Windows platform is required\n");
            return 77;
        }
        BOOL composition = FALSE;
        if (FAILED(DwmIsCompositionEnabled(&composition)) || !composition) {
            std::fprintf(stderr, "SKIPPED: DWM composition is unavailable\n");
            return 77;
        }
        testNativeFrame();
        if (!testMainWindow(vulkan)) return 77;
#else
        return 77;
#endif
    }
    std::puts("Window chrome tests passed");
    return EXIT_SUCCESS;
}
