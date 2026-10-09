// GUI 程序入口：装配应用服务、持久化、主题和主窗口，并串联音源检查与可选曲目导入。
#include "app/playerapplicationservice.h"
#include "app/settingsservice.h"
#include "app/ffmpegservice.h"
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

// 建立服务生命周期和信号连接后进入 Qt 事件循环，退出时按栈顺序释放对象。
int main(int argc, char* argv[])
{
    // QApplication 构造前加入随程序发布的平台插件路径，支持独立部署目录。
    const QString bundledPlatforms = QDir(QFileInfo(QString::fromLocal8Bit(argv[0])).absolutePath()).filePath(QStringLiteral("platforms"));
    if (QDir(bundledPlatforms).exists()) {
        QApplication::addLibraryPath(bundledPlatforms);
    }
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("MidiPlay"));
    app.setOrganizationName(QStringLiteral("MidiPlay"));
    app.setWindowIcon(QIcon(QStringLiteral(":/midi_play/branding/midiplay.ico")));

    // 设置先加载，再初始化检测服务、主题和播放器；主窗口借用这些长生命周期对象。
    midi_play::app::PlayerApplicationService service;
    auto settingsStore = std::make_unique<midi_play::infrastructure::settings::QSettingsStore>();
    midi_play::app::SettingsService settingsService(std::move(settingsStore));
    settingsService.load();
    midi_play::app::FfmpegService ffmpegService(&settingsService);
    midi_play::presentation::theme::ThemeController themeController(settingsService.themeMode());
    QObject::connect(&settingsService, &midi_play::app::SettingsService::themeModeChanged,
                     &themeController, &midi_play::presentation::theme::ThemeController::setMode);
    service.setVisualizationRefreshRate(settingsService.visualizationRefreshRate());
    service.setGraphicsMode(settingsService.graphicsMode());
    QObject::connect(&settingsService, &midi_play::app::SettingsService::visualizationRefreshRateChanged,
                     &service, &midi_play::app::PlayerApplicationService::setVisualizationRefreshRate);
    QObject::connect(&settingsService, &midi_play::app::SettingsService::graphicsModeChanged,
                     &service, &midi_play::app::PlayerApplicationService::setGraphicsMode);
    // 只持久化已经成功加载的音源候选，取消选择或加载失败均不覆盖旧路径。
    QObject::connect(&service, &midi_play::app::PlayerApplicationService::soundFontSelectionCommitted,
                     &settingsService, &midi_play::app::SettingsService::setSoundFontPath);

    midi_play::presentation::MainWindow window(&service, &settingsService, nullptr, &themeController, &ffmpegService);
    window.show();

    // 先显示主窗口，再安排一次启动检查；命令行曲目等待配置完成或用户跳过后导入。
    midi_play::presentation::settings::SoundFontSetup soundFontSetup(
        &settingsService, &service, &window, &themeController);
    if (argc > 1) {
        const QString inputPath = QString::fromLocal8Bit(argv[1]);
        QObject::connect(&soundFontSetup, &midi_play::presentation::settings::SoundFontSetup::finished,
                         &window, [&service, inputPath] {
            service.openFile(inputPath);
        });
    }
    // 在事件循环启动后执行，确保异步音源检查和非阻塞对话框有可用的事件派发。
    QTimer::singleShot(0, &soundFontSetup, &midi_play::presentation::settings::SoundFontSetup::start);

    return app.exec();
}
