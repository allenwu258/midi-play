#include "app/settingsservice.h"
#include "app/playerapplicationservice.h"
#include "domain/visualization/playbackvisualizationprojector.h"
#include "infrastructure/settings/qsettingsstore.h"
#include "presentation/settings/settingsdialog.h"
#include "presentation/mainwindow.h"
#include "presentation/transport/playbackratecontrol.h"
#include "presentation/visualization/fallingnotesview.h"

#if MIDI_PLAY_HAS_VULKAN
#include "presentation/visualization/fallingnotesvulkanwindow.h"
#endif

#include <QApplication>
#include <QComboBox>
#include <QCheckBox>
#include <QDir>
#include <QElapsedTimer>
#include <QImage>
#include <QFrame>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMouseEvent>
#include <QScreen>
#include <QSettings>
#include <QSlider>
#include <QSpinBox>
#include <QTemporaryDir>
#include <QThread>
#include <QToolButton>
#include <QWindow>

#include <cstdio>
#include <cstdlib>
#include <memory>

namespace {

using midi_play::settings::GraphicsMode;
using midi_play::playback::State;
using midi_play::presentation::visualization::FallingNotesView;

void require(bool condition, const char* message)
{
    if (!condition) {
        std::fprintf(stderr, "FAILED: %s\n", message);
        std::exit(EXIT_FAILURE);
    }
}

midi_play::visualization::VisualChartPtr makeChart()
{
    midi_play::music::MusicDocument document;
    document.tempos().push_back({0, 120.0, 0});
    document.setDuration(1920);
    midi_play::music::Track track;
    track.id = QStringLiteral("piano");
    track.measures.push_back({1, 0, 1920});
    midi_play::music::NoteEvent note;
    note.noteId = 1;
    note.start = 480;
    note.duration = 480;
    note.pitch = 60;
    note.velocity = 100;
    track.notes.push_back(note);
    document.tracks().push_back(track);
    document.rebuildMeasureGrid();
    auto chart = midi_play::visualization::PlaybackVisualizationProjector().project(document, 1);
    require(chart && !chart->notes().isEmpty(), "test chart must contain a visible note");
    return chart;
}

QImage capture(FallingNotesView& view)
{
    // grab() resolves pending parent layout changes before allocating the image.
    const QImage image = view.grab().toImage();
    require(image.pixelColor(image.width() / 2, image.height() / 2).alpha() == 255,
            "traditional view must paint an opaque frame");
    return image;
}

void testTraditionalViewUpdates()
{
    FallingNotesView view;
    view.resize(960, 640);
    view.setGraphicsMode(GraphicsMode::Traditional);
    const auto empty = capture(view);
    const auto chart = makeChart();
    view.setChart(chart);
    view.setTransportState(State::Playing);
    view.setTransportPosition(600'000, chart->durationUs());
    const auto playing = capture(view);
    require(playing != empty, "loading a chart must update the view");

    view.setTransportPosition(750'000, chart->durationUs());
    const auto moved = capture(view);
    require(moved != playing, "transport updates must move the rendered scene");
    view.setTransportState(State::Paused);
    const auto paused = capture(view);
    require(paused != moved, "pause must clear the active keyboard state");
    view.setLoading(true);
    require(capture(view) != paused, "loading must draw a status overlay");
    view.setLoading(false);
    require(capture(view) == paused, "leaving loading must restore the scene");
    view.setErrorMessage(QStringLiteral("Test error"));
    require(capture(view) != paused, "errors must be visible in traditional mode");
    view.setErrorMessage({});
    require(capture(view) == paused, "clearing an error must restore the scene");

#if !MIDI_PLAY_HAS_VULKAN
    view.setGraphicsMode(GraphicsMode::VulkanExperimental);
    require(capture(view) == paused,
            "an unavailable Vulkan preference must keep traditional rendering functional");
#endif
    view.setChart({});
    view.setTransportState(State::Empty);
    view.setTransportPosition(0, 0);
    require(capture(view) == empty, "clearing a chart must restore the empty view");
}

void testGraphicsModePreference()
{
    QTemporaryDir temporary;
    require(temporary.isValid(), "temporary settings directory must be available");
    const auto path = temporary.filePath(QStringLiteral("settings.ini"));
    {
        midi_play::app::SettingsService seed(
            std::make_unique<midi_play::infrastructure::settings::QSettingsStore>(path));
        seed.setGraphicsMode(GraphicsMode::VulkanExperimental);
    }
    midi_play::app::SettingsService service(
        std::make_unique<midi_play::infrastructure::settings::QSettingsStore>(path));
    service.load();
    midi_play::presentation::settings::SettingsDialog dialog(&service, nullptr);
    const auto* combo = dialog.findChild<QComboBox*>(QStringLiteral("graphicsModeCombo"));
    require(combo != nullptr, "settings must expose the graphics mode control");
#if MIDI_PLAY_HAS_VULKAN
    require(combo->isEnabled() && combo->findData(1) >= 0 && combo->currentData().toInt() == 1,
            "a Vulkan build must retain its graphics mode selection");
#else
    require(!combo->isEnabled() && combo->findData(1) == -1 && combo->currentData().toInt() == 0,
            "a traditional build must display only its available backend");
    require(!combo->toolTip().isEmpty(), "unavailable graphics mode needs an explanation");
#endif
    require(service.graphicsMode() == GraphicsMode::VulkanExperimental,
            "opening settings must not overwrite the saved Vulkan preference");
    service.setVisualizationRefreshRate(30);
    midi_play::infrastructure::settings::QSettingsStore persisted(path);
    require(persisted.load(nullptr).graphicsMode == GraphicsMode::VulkanExperimental,
            "saving unrelated settings must preserve the Vulkan preference");
}

void testNotationStripPreference()
{
    QTemporaryDir temporary;
    require(temporary.isValid(), "temporary settings directory must be available");
    const auto path = temporary.filePath(QStringLiteral("settings.ini"));
    midi_play::infrastructure::settings::QSettingsStore persisted(path);
    require(!persisted.load(nullptr).showNotationStrip, "new users must start with notation hidden");
    {
        QSettings legacy(path, QSettings::IniFormat);
        legacy.setValue(QStringLiteral("General/schemaVersion"), 4);
        legacy.setValue(QStringLiteral("General/visualizationRefreshRate"), 120);
    }
    midi_play::app::SettingsService settings(
        std::make_unique<midi_play::infrastructure::settings::QSettingsStore>(path));
    settings.load();
    require(!settings.showNotationStrip() && settings.visualizationRefreshRate() == 120,
            "older settings must default notation to hidden without resetting other preferences");
    midi_play::app::PlayerApplicationService player;
    midi_play::presentation::MainWindow window(&player, &settings);
    auto* view = window.findChild<FallingNotesView*>();
    require(view && !view->showNotationStrip(), "main window must apply the hidden default");
    const auto chart = makeChart();
    view->resize(960, 640);
    view->setChart(chart);
    view->setTransportState(State::Playing);
    view->setTransportPosition(600'000, chart->durationUs());
    const auto hidden = capture(*view);
    midi_play::presentation::settings::SettingsDialog dialog(&settings, nullptr);
    auto* control = dialog.findChild<QCheckBox*>(QStringLiteral("showNotationStripCheckBox"));
    require(control && control->isEnabled() && !control->isChecked(), "settings must expose the notation checkbox");
    int changes = 0;
    QObject::connect(&settings, &midi_play::app::SettingsService::showNotationStripChanged,
                     &dialog, [&](bool) { ++changes; });
    control->click();
    require(changes == 1 && settings.showNotationStrip() && view->showNotationStrip(),
            "one checkbox click must update the running view exactly once");
    require(capture(*view) != hidden, "showing notation must immediately relayout the raster scene");
    settings.setShowNotationStrip(true);
    require(changes == 1, "an unchanged preference must not emit another update");
    settings.setVisualizationRefreshRate(60);
    {
        midi_play::app::SettingsService restarted(
            std::make_unique<midi_play::infrastructure::settings::QSettingsStore>(path));
        restarted.load();
        midi_play::app::PlayerApplicationService nextPlayer;
        midi_play::presentation::MainWindow nextWindow(&nextPlayer, &restarted);
        require(restarted.showNotationStrip() && nextWindow.findChild<FallingNotesView*>()->showNotationStrip(),
                "restart must restore visible notation after saving an unrelated setting");
    }
    settings.setShowNotationStrip(false);
    require(changes == 2 && !control->isChecked() && !view->showNotationStrip(),
            "programmatic changes must synchronize the checkbox without feedback");
    require(capture(*view) == hidden, "hiding notation must restore the original geometry at the same song time");
    require(!persisted.load(nullptr).showNotationStrip, "hidden preference must persist across reloads");

    const auto args = QApplication::arguments();
    const int output = args.indexOf(QStringLiteral("--snapshots"));
    if (output >= 0 && output + 1 < args.size()) {
        const auto directory = args[output + 1];
        require(QDir().mkpath(directory), "settings snapshot directory must be writable");
        require(dialog.grab().save(directory + QStringLiteral("/settings-notation-hidden.png")),
                "settings snapshot must save");
    }
}

void processEventsFor(int milliseconds)
{
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < milliseconds) {
        QApplication::processEvents();
        QThread::msleep(1);
    }
}

void typePercent(QSpinBox* editor, const QString& digits)
{
    editor->selectAll();
    auto* lineEdit = editor->findChild<QLineEdit*>();
    require(lineEdit != nullptr, "percentage editor must accept text input");
    for (const auto digit : digits) {
        QKeyEvent key(QEvent::KeyPress, Qt::Key_0 + digit.digitValue(),
                      Qt::NoModifier, QString(digit));
        QApplication::sendEvent(lineEdit, &key);
    }
}

void testPlaybackRateInteraction(GraphicsMode mode = GraphicsMode::Traditional)
{
    midi_play::app::PlayerApplicationService service;
    midi_play::presentation::MainWindow window(&service, nullptr);
    auto* view = window.findChild<FallingNotesView*>();
    require(view != nullptr, "main window must expose the visualization");
    view->setGraphicsMode(mode);
    const auto chart = makeChart();
    view->setChart(chart);
    view->setTransportPosition(600'000, chart->durationUs());
    view->setTransportState(State::Playing);
    window.show();
    processEventsFor(30);
    auto* button = window.findChild<midi_play::presentation::PlaybackRateControl*>();
    auto* slider = window.findChild<QSlider*>(QStringLiteral("playbackRateSlider"));
    auto* editor = window.findChild<QSpinBox*>(QStringLiteral("playbackRateSpinBox"));
    auto* panel = window.findChild<QFrame*>(QStringLiteral("playbackRatePopup"));
    require(button && slider && editor && panel, "transport must expose all playback rate inputs");
    require(button->text() == QStringLiteral("100%") && service.playbackRatePercent() == 100,
            "playback must default to 100 percent");
    require(slider->minimum() == 20 && slider->maximum() == 200
                && editor->minimum() == 20 && editor->maximum() == 200,
            "both inputs must support the complete 20 to 200 percent range");
    int changes = 0;
    QObject::connect(&service, &midi_play::app::PlayerApplicationService::playbackRateChanged,
                     &window, [&](int) { ++changes; });
    slider->setValue(20);
    slider->setValue(200);
    require(service.playbackRatePercent() == 200 && editor->value() == 200
                && button->text() == QStringLiteral("200%") && changes == 2,
            "slider edits must apply once and synchronize the button and editor");
    service.setPlaybackRatePercent(80);
    require(slider->value() == 80 && editor->value() == 80 && changes == 3,
            "model changes must update both inputs without feedback");

    const QPoint buttonCenter = button->mapToGlobal(button->rect().center());
    QEnterEvent enter(button->rect().center(), window.mapFromGlobal(buttonCenter), buttonCenter);
    QApplication::sendEvent(button, &enter);
    require(panel->isVisible() && QApplication::activePopupWidget() == nullptr,
            "hover must open the panel without a modal popup mouse grab");
    require(button->screen()->availableGeometry().contains(panel->geometry()),
            "hover panel must stay within the available screen");
    require(!panel->geometry().intersects(QRect(button->mapToGlobal(QPoint()), button->size())),
            "hover panel must not cover its trigger");

    button->click();
    processEventsFor(30);
    typePercent(editor, QStringLiteral("125"));
    require(service.playbackRatePercent() == 80 && changes == 3,
            "partial keyboard input must not change playback speed");
    QKeyEvent commit(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
    QApplication::sendEvent(editor, &commit);
    require(service.playbackRatePercent() == 125 && slider->value() == 125
                && changes == 4 && !panel->isVisible(),
            "Enter must apply a complete percentage once and dismiss the editor");

    button->click();
    processEventsFor(30);
    typePercent(editor, QStringLiteral("50"));
    QKeyEvent cancel(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    QApplication::sendEvent(editor, &cancel);
    require(service.playbackRatePercent() == 125 && editor->value() == 125 && !panel->isVisible(),
            "Escape must discard an uncommitted percentage");

    button->click();
    processEventsFor(30);
    typePercent(editor, QStringLiteral("90"));
    // Match native clicking: the OS activates the target window before Qt
    // receives its mouse event. sendEvent alone does not perform activation.
    window.activateWindow();
    processEventsFor(30);
    const QPoint outside = window.mapToGlobal(QPoint(window.width() - 20, 100));
    QMouseEvent outsidePress(QEvent::MouseButtonPress, QPointF(window.width() - 20, 100),
                             outside, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(window.windowHandle(), &outsidePress);
    QMouseEvent outsideRelease(QEvent::MouseButtonRelease, QPointF(window.width() - 20, 100),
                               outside, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(window.windowHandle(), &outsideRelease);
    require(!panel->isVisible() && service.playbackRatePercent() == 90,
            "native window clicks outside the panel must commit and dismiss it");

    button->click();
    processEventsFor(30);
    require(panel->isVisible(), "click must reopen the rate panel after an outside click");
    slider->setSliderDown(true);
    slider->setValue(140);
    QEvent leave(QEvent::Leave);
    QApplication::sendEvent(panel, &leave);
    processEventsFor(260);
    if (!panel->isVisible() || service.playbackRatePercent() != 140) {
        std::fprintf(stderr, "Rate drag: visible=%d, down=%d, rate=%d, applicationState=%d\n",
                     panel->isVisible(), slider->isSliderDown(), service.playbackRatePercent(),
                     int(QGuiApplication::applicationState()));
    }
    require(panel->isVisible() && service.playbackRatePercent() == 140,
            "hover timeout must not interrupt an active slider drag");
    slider->setSliderDown(false);
    view->setTransportState(State::Paused);
    window.hide();
    require(!panel->isVisible(), "hiding the owner must also hide its rate panel");

    midi_play::app::PlayerApplicationService restarted;
    require(restarted.playbackRatePercent() == 100,
            "a new application service must start at 100 percent");
}

#if MIDI_PLAY_HAS_VULKAN
void testVulkanSwitching()
{
    using midi_play::presentation::visualization::FallingNotesVulkanWindow;
    FallingNotesView view;
    view.resize(960, 640);
    const auto chart = makeChart();
    view.setChart(chart);
    view.setTransportState(State::Paused);
    view.setTransportPosition(600'000, chart->durationUs());
    view.show();
    QApplication::processEvents();
    const auto traditional = capture(view);
    for (int cycle = 0; cycle < 2; ++cycle) {
        view.setShowNotationStrip(true);
        view.setGraphicsMode(GraphicsMode::VulkanExperimental);
        FallingNotesVulkanWindow* window = nullptr;
        for (auto* candidate : QGuiApplication::allWindows()) {
            if (auto* vulkan = qobject_cast<FallingNotesVulkanWindow*>(candidate)) window = vulkan;
        }
        require(window != nullptr, "Vulkan selection must create a Vulkan window");
        require(window->sceneState().showNotationStrip, "backend creation must retain the notation preference");
        int frames = 0;
        bool failed = false;
        QObject::connect(window, &FallingNotesVulkanWindow::frameRendered, &view, [&] { ++frames; });
        QObject::connect(window, &FallingNotesVulkanWindow::initializationFailed, &view,
                         [&](const QString&) { failed = true; });
        view.setTransportState(State::Playing);
        QElapsedTimer timer;
        timer.start();
        while (frames < 3 && !failed && timer.elapsed() < 5000) {
            QApplication::processEvents();
            QThread::msleep(10);
        }
        require(!failed && frames >= 3 && window->isValid(), "Vulkan must render successfully");
        require(window->supportsGrab(), "Vulkan smoke check requires swapchain readback");
        view.setTransportState(State::Paused);
        const auto notationShown = window->grab();
        view.setShowNotationStrip(false);
        const auto first = window->grab();
        require(!window->sceneState().showNotationStrip && first != notationShown,
                "hiding notation must update an existing Vulkan layout without resizing");
        view.setShowNotationStrip(true);
        require(window->grab() == notationShown, "Vulkan must restore the shown layout deterministically");
        view.setShowNotationStrip(false);
        view.setTransportPosition(850'000, chart->durationUs());
        const auto second = window->grab();
        require(!first.isNull() && !second.isNull() && first != second,
                "Vulkan readback must change when transport moves");
        view.setGraphicsMode(GraphicsMode::Traditional);
        view.setTransportPosition(600'000, chart->durationUs());
        require(capture(view) == traditional, "switching back must restore traditional rendering");
    }
}
#endif


void testMetronomeControl(GraphicsMode mode = GraphicsMode::Traditional)
{
    midi_play::app::PlayerApplicationService service;
    midi_play::presentation::MainWindow window(&service, nullptr);
    window.findChild<FallingNotesView*>()->setGraphicsMode(mode);
    auto* button = window.findChild<QToolButton*>(QStringLiteral("metronomeButton"));
    require(button && button->isCheckable() && !button->isChecked() && !button->isEnabled(),
            "metronome starts off and unavailable before song load");
    int changes = 0;
    QObject::connect(&service, &midi_play::app::PlayerApplicationService::metronomeChanged,
                     &window, [&](bool) { ++changes; });
    service.metronomeAvailabilityChanged(true, {});
    button->click();
    require(service.metronomeEnabled() && button->isChecked() && changes == 1,
            "metronome click updates service once");
    service.metronomeAvailabilityChanged(false, QStringLiteral("SMPTE"));
    require(!button->isEnabled() && button->isChecked() && service.metronomeEnabled()
            && button->toolTip().contains("SMPTE"), "unavailable song retains preference and shows reason");
    service.setMetronomeEnabled(false);
    require(!button->isChecked() && changes == 2, "programmatic state sync must not feed back");
    midi_play::app::PlayerApplicationService restarted;
    require(!restarted.metronomeEnabled(), "metronome preference is session-only");
}

} // namespace

int main(int argc, char* argv[])
{
    QApplication application(argc, argv);
    testTraditionalViewUpdates();
    testGraphicsModePreference();
    testNotationStripPreference();
    testPlaybackRateInteraction();
    testMetronomeControl();
    if (application.arguments().contains(QStringLiteral("--vulkan-smoke"))) {
#if MIDI_PLAY_HAS_VULKAN
        testVulkanSwitching();
        testMetronomeControl(GraphicsMode::VulkanExperimental);
        testPlaybackRateInteraction(GraphicsMode::VulkanExperimental);
#else
        require(false, "Vulkan smoke check requested for a traditional-only build");
#endif
    }
    std::puts("Presentation backend checks passed");
    return 0;
}
