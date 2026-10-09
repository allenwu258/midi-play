#pragma once

// 音源首次设置流程：复用文件选择动作，并在启动时验证已保存的音源。

#include <QDialog>
#include <QObject>

namespace midi_play::app { class PlayerApplicationService; class SettingsService; }
namespace midi_play::presentation::theme { class ThemeController; }

namespace midi_play::presentation::settings {

// 首次设置和设置页共用；提交异步加载请求，候选加载成功后才由外部连接持久化路径。
void chooseSoundFontFile(QWidget* parent, app::SettingsService* settings,
                         app::PlayerApplicationService* player);

// 允许选择或跳过的窗口模态对话框；音源加载期间禁止退出以保持启动顺序。
class SoundFontSetupDialog final : public QDialog {
    Q_OBJECT
public:
    SoundFontSetupDialog(app::SettingsService* settings, app::PlayerApplicationService* player,
                         QWidget* parent, theme::ThemeController* themeController = nullptr);
    // 加载结束后才接受取消或跳过；该加载请求不支持中途取消。
    void reject() override;

private:
    app::PlayerApplicationService* m_player;
};

// 启动检查只执行一次；用户跳过后，运行时音源失败不会再次打开首次设置窗口。
class SoundFontSetup final : public QObject {
    Q_OBJECT
public:
    SoundFontSetup(app::SettingsService* settings, app::PlayerApplicationService* player,
                   QWidget* parent, theme::ThemeController* themeController = nullptr);
    // 无已保存路径则显示对话框，否则异步验证保存的音源。
    void start();

signals:
    // 已保存音源验证成功，或用户完成/跳过对话框后，允许继续启动流程。
    void finished();

private:
    // 非阻塞打开自动销毁的对话框，结束时通知启动协调方。
    void showDialog();
    app::SettingsService* m_settings;
    app::PlayerApplicationService* m_player;
    QWidget* m_window;
    theme::ThemeController* m_themeController;
    bool m_started = false;
    // 只消费启动验证请求的完成信号，忽略之后运行时的音源加载结果。
    bool m_checkPending = false;
};

} // namespace midi_play::presentation::settings
