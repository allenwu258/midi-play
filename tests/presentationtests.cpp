#include "app/settingsservice.h"
#include "domain/visualization/playbackvisualizationprojector.h"
#include "infrastructure/settings/qsettingsstore.h"
#include "presentation/settings/settingsdialog.h"
#include "presentation/visualization/fallingnotesview.h"

#if MIDI_PLAY_HAS_VULKAN
#include "presentation/visualization/fallingnotesvulkanwindow.h"
#endif

#include <QApplication>
#include <QComboBox>
#include <QElapsedTimer>
#include <QImage>
#include <QTemporaryDir>
#include <QThread>

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
    QImage image(view.size(), QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    view.render(&image);
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
        view.setGraphicsMode(GraphicsMode::VulkanExperimental);
        FallingNotesVulkanWindow* window = nullptr;
        for (auto* candidate : QGuiApplication::allWindows()) {
            if (auto* vulkan = qobject_cast<FallingNotesVulkanWindow*>(candidate)) window = vulkan;
        }
        require(window != nullptr, "Vulkan selection must create a Vulkan window");
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
        const auto first = window->grab();
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

} // namespace

int main(int argc, char* argv[])
{
    QApplication application(argc, argv);
    testTraditionalViewUpdates();
    testGraphicsModePreference();
    if (application.arguments().contains(QStringLiteral("--vulkan-smoke"))) {
#if MIDI_PLAY_HAS_VULKAN
        testVulkanSwitching();
#else
        require(false, "Vulkan smoke check requested for a traditional-only build");
#endif
    }
    std::puts("Presentation backend checks passed");
    return 0;
}
