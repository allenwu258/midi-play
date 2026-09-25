#include "app/playerapplicationservice.h"
#include "app/settingsservice.h"
#include "infrastructure/settings/qsettingsstore.h"
#include "presentation/mainwindow.h"
#include "presentation/settings/settingsdialog.h"
#include "presentation/settings/soundfontsetup.h"

#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QTemporaryDir>
#include <QThread>
#include <QToolButton>

#include <cstdio>
#include <cstdlib>
#include <functional>
#include <memory>

namespace {
using namespace midi_play;
using presentation::settings::SoundFontSetup;
using presentation::settings::SoundFontSetupDialog;
using infrastructure::settings::QSettingsStore;
QString snapshotDirectory;

void require(bool condition, const char* message)
{
    if (condition) return;
    std::fprintf(stderr, "FAILED: %s\n", message);
    std::exit(EXIT_FAILURE);
}

bool waitUntil(const std::function<bool()>& condition)
{
    QElapsedTimer timer;
    timer.start();
    while (!condition() && timer.elapsed() < 30'000) {
        QApplication::processEvents();
        QThread::msleep(5);
    }
    return condition();
}

struct Harness {
    QTemporaryDir directory;
    app::SettingsService settings{std::make_unique<QSettingsStore>(directory.filePath("settings.ini"))};
    app::PlayerApplicationService player;
    std::unique_ptr<presentation::MainWindow> window;
    std::unique_ptr<SoundFontSetup> setup;
    int finished = 0;

    explicit Harness(const QString& configured = {})
    {
        require(directory.isValid(), "temporary settings directory");
        // Simulate the existing persisted key, including a missing user file.
        QSettings old(directory.filePath("settings.ini"), QSettings::IniFormat);
        old.setValue("Audio/soundFontPath", configured);
        old.sync();
        settings.load();
        QObject::connect(&player, &app::PlayerApplicationService::soundFontSelectionCommitted,
                         &settings, &app::SettingsService::setSoundFontPath);
        QObject::connect(&player, &app::PlayerApplicationService::errorOccurred, &player,
            [](const QString&) { require(false, "SoundFont problems must not use the modal error channel"); });
        window = std::make_unique<presentation::MainWindow>(&player, &settings);
        window->show();
        require(!window->findChild<QToolButton*>("exportButton")->isEnabled(),
                "audio export is disabled before loading a score");
        setup = std::make_unique<SoundFontSetup>(&settings, &player, window.get());
        QObject::connect(setup.get(), &SoundFontSetup::finished, &player, [this] { ++finished; });
    }

    SoundFontSetupDialog* dialog() const { return window->findChild<SoundFontSetupDialog*>(); }

    void open(const QString& fixture)
    {
        player.openFile(fixture);
        require(waitUntil([this] { return player.session() != nullptr; }), "score must load without a SoundFont");
        require(window->findChild<QToolButton*>("exportButton")->isEnabled(),
                "audio export becomes available after loading a score");
    }
};

void testSkipAndInlinePlaybackError(const QString& fixture)
{
    Harness h;
    h.setup->start();
    require(h.dialog() && h.dialog()->isVisible(), "unconfigured startup prompts once");
    if (!snapshotDirectory.isEmpty()) {
        for (const auto mode : {settings::ThemeMode::Dark, settings::ThemeMode::Light}) {
            h.settings.setThemeMode(mode);
            QApplication::processEvents();
            require(h.dialog()->grab().save(snapshotDirectory + (mode == settings::ThemeMode::Dark
                ? "/setup-dark.png" : "/setup-light.png")), "save startup preview");
        }
    }
    h.dialog()->findChild<QPushButton*>("skipSoundFontButton")->click();
    require(h.finished == 1 && !h.player.hasSoundFont() && h.settings.soundFontPath().isEmpty(),
            "skipping completes startup without writing a fake default path");
    h.setup->start();
    require(h.finished == 1, "startup check is idempotent");
    h.open(fixture);
    auto* session = h.player.session();
    for (int i = 0; i < 2; ++i) h.player.play();
    require(h.player.playbackState() == playback::State::Ready && h.player.positionMicroseconds() == 0,
            "missing font must not advance playback or put transport in Error");
    auto* error = h.window->findChild<QLabel*>("soundFontError");
    require(error && error->isVisible() && error->text().contains("SF2/SF3"), "main window shows actionable inline error");
    require(!h.window->findChild<QMessageBox*>(), "playback must not create an error popup");
    require(!h.dialog() || !h.dialog()->isVisible(), "runtime failures must not reopen onboarding");
    require(h.player.session() == session, "failed play preserves the imported score");
    presentation::settings::SettingsDialog settings(&h.settings, &h.player, h.window.get());
    require(settings.findChild<QLabel*>("settingsError")->text() == error->text(),
            "opening settings after failure must retain the error");
    require(!settings.findChild<QPushButton*>("resetSoundFontButton"), "no restore-default action remains");
    require(!settings.findChild<QLineEdit*>("soundFontPathEdit")->placeholderText().isEmpty(),
            "unconfigured settings show a useful placeholder");
    if (!snapshotDirectory.isEmpty()) {
        require(h.window->grab().save(snapshotDirectory + "/inline-error.png"), "save inline error preview");
        require(settings.grab().save(snapshotDirectory + "/settings.png"), "save settings preview");
    }
}

void testInvalidStartup()
{
    QTemporaryDir files;
    const auto missing = files.filePath("missing.sf2");
    const auto corrupt = files.filePath("corrupt.sf3");
    QFile file(corrupt);
    require(file.open(QIODevice::WriteOnly) && file.write("not a soundfont") == 15, "create corrupt font");
    file.close();
    for (const auto& path : {missing, corrupt}) {
        Harness h(path);
        h.setup->start();
        require(waitUntil([&] { return h.dialog() != nullptr; }), "missing/corrupt configured font prompts");
        require(!h.player.hasSoundFont() && !h.player.lastSoundFontError().isEmpty(),
                "invalid startup font has a diagnostic and is not accepted");
        require(h.settings.soundFontPath() == path, "failed startup validation preserves the user's preference");
        h.dialog()->reject();
        require(h.finished == 1, "closing setup is equivalent to skip");
    }
}

void testValidStartupAndLateConfiguration(const QString& fixture, const QString& font)
{
    QTemporaryDir files;
    const QString copied = files.filePath("external.sf3");
    require(QFile::copy(font, copied), "copy external fixture for removal test");
    {
        Harness configured(copied);
        configured.setup->start();
        configured.player.play();
        require(configured.finished == 0 && !configured.dialog(), "play during startup validation does not interrupt it");
        require(waitUntil([&] { return configured.finished == 1; }), "configured font passes backend validation");
        require(configured.player.hasSoundFont() && !configured.dialog(), "valid startup never shows onboarding");
    }
    Harness h;
    h.setup->start();
    h.dialog()->reject();
    h.open(fixture);
    auto* session = h.player.session();
    h.player.requestSoundFontLoad(copied);
    require(waitUntil([&] { return !h.player.isSoundFontLoading(); }), "late font selection completes");
    require(h.player.hasSoundFont() && h.player.session() == session, "late selection initializes the existing session");
    QSettingsStore store(h.directory.filePath("settings.ini"));
    require(store.load(nullptr).soundFontPath == copied, "only successful selection is persisted");
    h.player.play();
    require(waitUntil([&] { return h.player.positionMicroseconds() > 100'000; }), "play works without reimport after setup");
    h.player.pause();
    require(waitUntil([&] { return h.player.playbackState() == playback::State::Paused; }), "pause completes");
    h.player.requestSoundFontLoad(files.filePath("missing.sf2"));
    require(h.player.hasSoundFont() && h.settings.soundFontPath() == copied, "failed replacement retains working font");
    const auto corrupt = files.filePath("broken.sf2");
    QFile bad(corrupt);
    require(bad.open(QIODevice::WriteOnly) && bad.write("bad") == 3, "create invalid replacement");
    bad.close();
    h.player.requestSoundFontLoad(corrupt);
    require(waitUntil([&] { return !h.player.isSoundFontLoading(); }), "invalid replacement completes asynchronously");
    require(!h.player.lastSoundFontError().isEmpty() && h.settings.soundFontPath() == copied,
            "decoder failure must preserve the persisted working selection");
    h.player.play();
    require(waitUntil([&] { return h.player.playbackState() == playback::State::Playing; }), "old font remains playable");
    h.player.pause();
    require(waitUntil([&] { return h.player.playbackState() == playback::State::Paused; }), "second pause completes");
    require(QFile::rename(copied, copied + ".moved"), "simulate externally moved font");
    h.player.play();
    require(h.player.playbackState() == playback::State::Paused && !h.player.lastSoundFontError().isEmpty(),
            "missing runtime font prevents silent play with inline feedback");
}

void testSetupSelectionAndLoadingGuard(const QString& font)
{
    Harness h;
    h.setup->start();
    h.player.requestSoundFontLoad(font);
    require(h.player.isSoundFontLoading(), "validation is asynchronous");
    h.dialog()->reject();
    require(h.finished == 0, "cannot race an in-flight validation by skipping");
    // A play-time diagnostic must not be mistaken for startup completion.
    h.player.play();
    require(h.finished == 0, "inline errors do not complete font selection");
    require(waitUntil([&] { return h.finished == 1; }), "successful setup closes the dialog");
    require(h.player.hasSoundFont() && !h.settings.soundFontPath().isEmpty(), "setup persists selected font");
    require(h.player.lastSoundFontError().isEmpty(), "successful selection clears stale feedback");
}
} // namespace

int main(int argc, char** argv)
{
    QApplication application(argc, argv);
    const auto args = application.arguments();
    const int snapshots = args.indexOf("--snapshots");
    if (snapshots >= 0) {
        snapshotDirectory = args.value(snapshots + 1);
        require(!snapshotDirectory.isEmpty() && QDir().mkpath(snapshotDirectory), "preview directory is writable");
    }
    const auto fixture = args.value(args.indexOf("--fixture") + 1);
    require(QFile::exists(fixture), "music fixture is required");
    testSkipAndInlinePlaybackError(fixture);
    testInvalidStartup();
    const int fontIndex = args.indexOf("--soundfont");
    if (fontIndex >= 0) {
        const auto font = args.value(fontIndex + 1);
        testValidStartupAndLateConfiguration(fixture, font);
        testSetupSelectionAndLoadingGuard(font);
    }
    std::puts("SoundFont startup, skip, inline errors and selection checks passed");
}
