#include "app/playerapplicationservice.h"
#include "app/settingsservice.h"
#include "infrastructure/settings/qsettingsstore.h"
#include "presentation/settings/soundfontsetup.h"
#include "presentation/mainwindow.h"
#include "presentation/theme/themecontroller.h"

#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QIcon>
#include <QTimer>

#include <memory>

int main(int argc, char* argv[])
{
    const QString bundledPlatforms = QDir(QFileInfo(QString::fromLocal8Bit(argv[0])).absolutePath()).filePath(QStringLiteral("platforms"));
    if (QDir(bundledPlatforms).exists()) {
        QApplication::addLibraryPath(bundledPlatforms);
    }
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("MidiPlay"));
    app.setOrganizationName(QStringLiteral("MidiPlay"));
    app.setWindowIcon(QIcon(QStringLiteral(":/midi_play/branding/midiplay.ico")));

    midi_play::app::PlayerApplicationService service;
    auto settingsStore = std::make_unique<midi_play::infrastructure::settings::QSettingsStore>();
    midi_play::app::SettingsService settingsService(std::move(settingsStore));
    settingsService.load();
    midi_play::presentation::theme::ThemeController themeController(settingsService.themeMode());
    QObject::connect(&settingsService, &midi_play::app::SettingsService::themeModeChanged,
                     &themeController, &midi_play::presentation::theme::ThemeController::setMode);
    service.setVisualizationRefreshRate(settingsService.visualizationRefreshRate());
    service.setGraphicsMode(settingsService.graphicsMode());
    QObject::connect(&settingsService, &midi_play::app::SettingsService::visualizationRefreshRateChanged,
                     &service, &midi_play::app::PlayerApplicationService::setVisualizationRefreshRate);
    QObject::connect(&settingsService, &midi_play::app::SettingsService::graphicsModeChanged,
                     &service, &midi_play::app::PlayerApplicationService::setGraphicsMode);
    QObject::connect(&service, &midi_play::app::PlayerApplicationService::soundFontSelectionCommitted,
                     &settingsService, &midi_play::app::SettingsService::setSoundFontPath);

    midi_play::presentation::MainWindow window(&service, &settingsService, nullptr, &themeController);
    window.show();

    midi_play::presentation::settings::SoundFontSetup soundFontSetup(
        &settingsService, &service, &window, &themeController);
    if (argc > 1) {
        const QString inputPath = QString::fromLocal8Bit(argv[1]);
        QObject::connect(&soundFontSetup, &midi_play::presentation::settings::SoundFontSetup::finished,
                         &window, [&service, inputPath] {
            service.openFile(inputPath);
        });
    }
    QTimer::singleShot(0, &soundFontSetup, &midi_play::presentation::settings::SoundFontSetup::start);

    return app.exec();
}
