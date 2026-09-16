#include "app/playerapplicationservice.h"
#include "app/settingsservice.h"
#include "domain/visualization/playbackvisualizationprojector.h"
#include "infrastructure/settings/qsettingsstore.h"
#include "presentation/mainwindow.h"
#include "presentation/settings/settingsdialog.h"
#include "presentation/theme/themecontroller.h"
#include "presentation/theme/themeicons.h"
#include "presentation/transport/playbackratecontrol.h"
#include "presentation/visualization/fallingnotesview.h"

#include <QAbstractButton>
#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QFrame>
#include <QLabel>
#include <QSettings>
#include <QSpinBox>
#include <QTemporaryDir>
#include <cstdio>
#include <cstdlib>
#include <memory>

namespace {
using midi_play::app::SettingsService;
using midi_play::infrastructure::settings::QSettingsStore;
using midi_play::presentation::theme::ThemeController;
using midi_play::presentation::theme::themeFor;
using midi_play::presentation::visualization::FallingNotesView;
using midi_play::settings::ThemeMode;

void require(bool condition, const char* message)
{
    if (!condition) { std::fprintf(stderr, "FAILED: %s\n", message); std::exit(EXIT_FAILURE); }
}

void testPersistence()
{
    QTemporaryDir directory;
    require(directory.isValid(), "temporary settings directory must be available");
    const auto path = directory.filePath(QStringLiteral("settings.ini"));
    QSettingsStore store(path);
    QString warning;
    require(store.load(&warning).themeMode == ThemeMode::Dark && warning.isEmpty(),
            "a missing theme preference must default to dark without a warning");
    {
        QSettings legacy(path, QSettings::IniFormat);
        legacy.setValue(QStringLiteral("General/schemaVersion"), 5);
        legacy.setValue(QStringLiteral("General/visualizationRefreshRate"), 120);
        legacy.setValue(QStringLiteral("General/showNotationStrip"), true);
    }
    SettingsService service(std::make_unique<QSettingsStore>(path));
    service.load();
    require(service.themeMode() == ThemeMode::Dark && service.visualizationRefreshRate() == 120
                && service.showNotationStrip(), "legacy migration must preserve existing preferences");
    int changes = 0;
    QObject::connect(&service, &SettingsService::themeModeChanged, &service, [&](ThemeMode) { ++changes; });
    service.setThemeMode(ThemeMode::Light);
    service.setThemeMode(ThemeMode::Light);
    require(changes == 1, "an unchanged theme must not emit a duplicate update");
    service.setVisualizationRefreshRate(60);
    SettingsService restarted(std::make_unique<QSettingsStore>(path));
    restarted.load();
    require(restarted.themeMode() == ThemeMode::Light && restarted.showNotationStrip()
                && restarted.visualizationRefreshRate() == 60,
            "theme selection must survive restart and unrelated preference changes");
    require(QSettings(path, QSettings::IniFormat).value(QStringLiteral("General/schemaVersion")).toInt()
                == midi_play::settings::kSettingsSchemaVersion,
            "new settings must persist the current schema");
    {
        ThemeController startup(restarted.themeMode());
        midi_play::app::PlayerApplicationService player;
        midi_play::presentation::MainWindow window(&player, &restarted, nullptr, &startup);
        require(window.findChild<FallingNotesView*>()->themeMode() == ThemeMode::Light
                    && window.palette().color(QPalette::Window) == themeFor(ThemeMode::Light).widgets.panel,
                "startup must apply the saved theme before showing the first frame");
    }
    for (const QVariant value : {QVariant(-1), QVariant(99), QVariant(QStringLiteral("invalid"))}) {
        {
            QSettings invalid(path, QSettings::IniFormat);
            invalid.setValue(QStringLiteral("General/themeMode"), value);
        }
        restarted.load();
        require(restarted.themeMode() == ThemeMode::Dark && !restarted.lastLoadWarning().isEmpty()
                    && restarted.showNotationStrip(),
                "invalid theme values must warn and fall back without resetting other preferences");
    }
    midi_play::presentation::settings::SettingsDialog warningDialog(&restarted, nullptr);
    const auto* warningLabel = warningDialog.findChild<QLabel*>(QStringLiteral("settingsError"));
    require(warningLabel && !warningLabel->isHidden() && !warningLabel->text().isEmpty(),
            "startup load warnings must remain accessible in settings after playback status changes");
    service.setThemeMode(static_cast<ThemeMode>(99));
    require(service.themeMode() == ThemeMode::Dark && changes == 2,
            "invalid programmatic theme values must normalize to dark");
}

class UnwritableSettingsStore final : public midi_play::app::ISettingsStore {
public:
    midi_play::settings::PlayerSettings load(QString*) override { return {}; }
    bool save(const midi_play::settings::PlayerSettings&, QString* error) override
    {
        if (error) *error = QStringLiteral("Test settings write failure");
        return false;
    }
};

void testSaveFailure()
{
    SettingsService service(std::make_unique<UnwritableSettingsStore>());
    midi_play::presentation::settings::SettingsDialog dialog(&service, nullptr);
    auto* combo = dialog.findChild<QComboBox*>(QStringLiteral("themeModeCombo"));
    auto* error = dialog.findChild<QLabel*>(QStringLiteral("settingsError"));
    int failures = 0;
    QObject::connect(&service, &SettingsService::settingsSaveFailed, &dialog, [&](const QString&) { ++failures; });
    combo->setCurrentIndex(combo->findData(int(ThemeMode::Light)));
    require(failures == 1 && service.themeMode() == ThemeMode::Light
                && error && !error->isHidden() && !error->text().isEmpty(),
            "save failures must remain visible while the selected theme stays active for this session");
}

midi_play::visualization::VisualChartPtr sampleChart()
{
    midi_play::music::MusicDocument document;
    document.tempos().push_back({0, 120.0, 0});
    document.setDuration(5760);
    midi_play::music::Track track;
    track.id = QStringLiteral("piano");
    track.measures.push_back({1, 0, 5760});
    for (int pitch = 48; pitch <= 84; ++pitch) {
        midi_play::music::NoteEvent note;
        note.noteId = quint64(pitch);
        note.pitch = pitch;
        note.start = (pitch % 7) * 180;
        note.duration = pitch % 3 ? 240 : 1920;
        note.sustainEnd = note.start + 3000;
        note.velocity = 76;
        track.notes.push_back(note);
    }
    document.tracks().push_back(track);
    document.rebuildMeasureGrid();
    return midi_play::visualization::PlaybackVisualizationProjector().project(document, 1);
}

void testRuntimeTheme(const QString& snapshotDirectory)
{
    QTemporaryDir directory;
    require(directory.isValid(), "temporary settings directory must be available");
    SettingsService settings(std::make_unique<QSettingsStore>(directory.filePath(QStringLiteral("settings.ini"))));
    settings.load();
    ThemeController controller(settings.themeMode());
    QObject::connect(&settings, &SettingsService::themeModeChanged, &controller, &ThemeController::setMode);
    midi_play::app::PlayerApplicationService player;
    midi_play::presentation::MainWindow window(&player, &settings, nullptr, &controller);
    midi_play::presentation::settings::SettingsDialog dialog(&settings, &player, &window, &controller);
    auto* combo = dialog.findChild<QComboBox*>(QStringLiteral("themeModeCombo"));
    auto* view = window.findChild<FallingNotesView*>();
    require(combo && combo->count() == 2 && combo->currentData().toInt() == 0 && view,
            "settings must expose dark and light themes, defaulting to dark");
    const auto chart = sampleChart();
    view->setChart(chart);
    view->setTransportPosition(500'000, chart->durationUs());
    view->setTransportState(midi_play::playback::State::Playing);
    window.show();
    QApplication::processEvents();
    const auto darkScene = view->grab().toImage();
    const auto windowId = window.winId();
    const auto windowSize = window.size();
    const auto session = player.session();
    int playbackChanges = 0;
    QObject::connect(&player, &midi_play::app::PlayerApplicationService::playbackStateChanged, &window,
                     [&](auto) { ++playbackChanges; });
    QObject::connect(&player, &midi_play::app::PlayerApplicationService::playbackDiscontinuity, &window,
                     [&](auto) { ++playbackChanges; });
    QObject::connect(&player, &midi_play::app::PlayerApplicationService::soundFontLoadingChanged, &window,
                     [&](auto) { ++playbackChanges; });
    int themeChanges = 0;
    QObject::connect(&controller, &ThemeController::themeChanged, &window, [&](auto) { ++themeChanges; });
    combo->setCurrentIndex(combo->findData(int(ThemeMode::Light)));
    QApplication::processEvents();
    require(themeChanges == 1 && controller.mode() == ThemeMode::Light && view->themeMode() == ThemeMode::Light,
            "selecting light must update all consumers exactly once");
    require(view->grab().toImage() != darkScene, "the existing view must immediately display the new theme");
    require(window.winId() == windowId && window.size() == windowSize && player.session() == session
                && playbackChanges == 0, "theme changes must preserve the native window, size and playback session");
    require(QApplication::palette().color(QPalette::Window) == themeFor(ThemeMode::Light).widgets.panel,
            "native controls must receive the application theme palette");
    midi_play::presentation::settings::SettingsDialog newlyOpened(&settings, &player, &window, &controller);
    require(newlyOpened.findChild<QComboBox*>(QStringLiteral("themeModeCombo"))->currentData().toInt() == 1
                && newlyOpened.palette().color(QPalette::Window) == themeFor(ThemeMode::Light).widgets.panel,
            "newly opened windows must start with the active theme");
    settings.setThemeMode(ThemeMode::Dark);
    require(combo->currentData().toInt() == 0 && themeChanges == 2 && view->grab().toImage() == darkScene,
            "a theme round trip must restore the scene, including cached piano colors");

    if (!snapshotDirectory.isEmpty()) {
        for (auto mode : {ThemeMode::Dark, ThemeMode::Light}) {
            settings.setThemeMode(mode);
            const auto path = snapshotDirectory + (mode == ThemeMode::Dark ? QStringLiteral("/dark") : QStringLiteral("/light"));
            require(QDir().mkpath(path), "snapshot directory must be writable");
            require(window.grab().save(path + QStringLiteral("/main-window.png")), "main window snapshot must save");
            const auto normalSize = window.size();
            window.resize(window.minimumSizeHint());
            require(window.grab().save(path + QStringLiteral("/main-minimum.png")), "minimum size snapshot must save");
            window.resize(normalSize);
            require(dialog.grab().save(path + QStringLiteral("/settings.png")), "settings snapshot must save");
            settings.setVisualizationRefreshRate(75);
            auto* errorLabel = dialog.findChild<QLabel*>(QStringLiteral("settingsError"));
            errorLabel->setText(QStringLiteral("无法保存设置：当前配置目录不可写，请检查目录权限后重试。"));
            errorLabel->show();
            dialog.show();
            QApplication::processEvents();
            for (auto* control : dialog.findChildren<QWidget*>()) {
                if (control->isVisibleTo(&dialog) && (qobject_cast<QComboBox*>(control)
                        || qobject_cast<QAbstractButton*>(control) || qobject_cast<QSpinBox*>(control))) {
                    require(dialog.rect().contains(QRect(control->mapTo(&dialog, QPoint()), control->size())),
                            "expanded settings must keep interactive controls inside the dialog");
                }
            }
            require(dialog.grab().save(path + QStringLiteral("/settings-expanded.png")),
                    "settings with custom refresh rate and an error must save");
            errorLabel->hide();
            settings.setVisualizationRefreshRate(60);
            dialog.hide();
            auto* rate = window.findChild<midi_play::presentation::PlaybackRateControl*>();
            auto* panel = window.findChild<QFrame*>(QStringLiteral("playbackRatePopup"));
            rate->click();
            require(panel->grab().save(path + QStringLiteral("/rate-panel.png")), "rate panel snapshot must save");
            panel->hide();
        }
    }
    if (midi_play::settings::isCustomTitleBarAvailable()) {
        settings.setTitleBarMode(midi_play::settings::TitleBarMode::Custom);
        QApplication::processEvents();
        const auto customWindowId = window.winId();
        settings.setThemeMode(ThemeMode::Dark);
        settings.setThemeMode(ThemeMode::Light);
        require(window.winId() == customWindowId, "theme switching must retain the custom title bar window handle");
        if (!snapshotDirectory.isEmpty())
            require(window.grab().save(snapshotDirectory + QStringLiteral("/light/custom-titlebar.png")),
                    "custom title bar snapshot must save");
    }
    settings.setThemeMode(ThemeMode::Dark);
    window.hide();
}

void testIcons()
{
    using midi_play::presentation::theme::IconGlyph;
    using midi_play::presentation::theme::themedIcon;
    for (auto mode : {ThemeMode::Dark, ThemeMode::Light}) {
        for (qreal dpr : {1.0, 1.5, 2.0}) {
            auto icon = themedIcon(IconGlyph::Open, themeFor(mode));
            const auto normal = icon.pixmap(QSize(18, 18), dpr, QIcon::Normal);
            const auto disabled = icon.pixmap(QSize(18, 18), dpr, QIcon::Disabled);
            require(normal.size() == QSize(qRound(18 * dpr), qRound(18 * dpr))
                        && normal.devicePixelRatio() == dpr && normal.toImage() != disabled.toImage(),
                    "icons must retain device-pixel resolution and explicit disabled colors");
        }
    }
}
} // namespace

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    testPersistence();
    testSaveFailure();
    testIcons();
    const auto args = app.arguments();
    const int snapshot = args.indexOf(QStringLiteral("--snapshots"));
    testRuntimeTheme(snapshot >= 0 ? args.value(snapshot + 1) : QString());
    std::puts("Theme persistence, failure handling, runtime state and DPI checks passed");
}
