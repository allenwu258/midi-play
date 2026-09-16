#include "app/playerapplicationservice.h"
#include "app/settingsservice.h"
#include "infrastructure/settings/qsettingsstore.h"
#include "infrastructure/resources/defaultsoundfontlocator.h"
#include "presentation/mainwindow.h"
#include "presentation/theme/themecontroller.h"

#include <QApplication>
#include <QDir>
#include <QFileInfo>
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

    midi_play::app::PlayerApplicationService service;
    auto settingsStore = std::make_unique<midi_play::infrastructure::settings::QSettingsStore>();
    const QString defaultSoundFontPath =
        midi_play::resources::DefaultSoundFontLocator::locate();
    midi_play::app::SettingsService settingsService(
        std::move(settingsStore), defaultSoundFontPath);
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

    if (!service.loadSoundFont(settingsService.soundFontPath())
        && !settingsService.usesDefaultSoundFont()) {
        // A moved or deleted custom file must not leave the player silent.
        // A temporarily unavailable external SoundFont must not erase the
        // user's preference. This fallback applies only to this process.
        service.loadFallbackSoundFont(settingsService.defaultSoundFontPath());
    }
    if (argc > 1) {
        const QString inputPath = QString::fromLocal8Bit(argv[1]);
        QTimer::singleShot(0, &window, [&service, inputPath] {
            service.openFile(inputPath);
        });
    }

    return app.exec();
}
