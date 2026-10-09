// 音源设置实现：异步验证候选，展示失败原因，并将结果接入一次性启动检查。
#include "soundfontsetup.h"

#include "app/playerapplicationservice.h"
#include "app/settingsservice.h"
#include "presentation/theme/themecontroller.h"
#include "presentation/theme/widgetstyles.h"

#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

namespace midi_play::presentation::settings {

void chooseSoundFontFile(QWidget* parent, app::SettingsService* settings,
                         app::PlayerApplicationService* player)
{
    // 防止重复选择与加载；保存路径只用于定位文件对话框的初始目录。
    if (!settings || !player || player->isSoundFontLoading()) return;
    const QString current = settings->soundFontPath();
    const QString path = QFileDialog::getOpenFileName(parent, QStringLiteral("选择音源"),
        current.isEmpty() ? QString() : QFileInfo(current).absolutePath(),
        QStringLiteral("SoundFont 音源 (*.sf2 *.sf3)"));
    if (!path.isEmpty()) player->requestSoundFontLoad(path);
}

SoundFontSetupDialog::SoundFontSetupDialog(app::SettingsService* settings,
    app::PlayerApplicationService* player, QWidget* parent, theme::ThemeController* themeController)
    : QDialog(parent), m_player(player)
{
    setObjectName(QStringLiteral("soundFontSetupDialog"));
    setWindowTitle(QStringLiteral("配置播放音源"));
    setWindowModality(Qt::WindowModal);
    resize(480, 220);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(20, 18, 20, 18);
    layout->setSpacing(14);
    auto* title = new QLabel(QStringLiteral("选择你的播放音色"), this);
    title->setObjectName(QStringLiteral("settingsTitle"));
    layout->addWidget(title);
    auto* description = new QLabel(QStringLiteral(
        "MIDI Play 不附带乐曲音源。播放音乐前，请选择本地 SF2 或 SF3 音源文件。\n"
        "你可以先跳过并查看乐曲，之后在设置中配置音源。"), this);
    description->setWordWrap(true);
    layout->addWidget(description);
    auto* error = new QLabel(player->lastSoundFontError(), this);
    error->setObjectName(QStringLiteral("settingsError"));
    error->setTextFormat(Qt::PlainText);
    error->setWordWrap(true);
    error->setVisible(!error->text().isEmpty());
    layout->addWidget(error);
    auto* buttons = new QDialogButtonBox(this);
    auto* select = buttons->addButton(QStringLiteral("选择音源…"), QDialogButtonBox::ActionRole);
    select->setObjectName(QStringLiteral("setupSelectSoundFontButton"));
    select->setDefault(true);
    auto* skip = buttons->addButton(QStringLiteral("暂时跳过"), QDialogButtonBox::RejectRole);
    skip->setObjectName(QStringLiteral("skipSoundFontButton"));
    skip->setAutoDefault(false);
    layout->addWidget(buttons);
    connect(select, &QPushButton::clicked, this, [this, settings, player] {
        chooseSoundFontFile(this, settings, player);
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &SoundFontSetupDialog::reject);
    // 检查期间同时禁用选择和跳过；成功接受对话框，失败保留窗口供重试。
    connect(player, &app::PlayerApplicationService::soundFontLoadingChanged, this,
        [select, skip, error](bool loading) {
            select->setEnabled(!loading);
            skip->setEnabled(!loading);
            error->setText(loading ? QStringLiteral("正在检查音源，请稍候…") : QString());
            error->setVisible(loading);
        });
    connect(player, &app::PlayerApplicationService::soundFontLoadFailed, this,
        [error](const QString& message) { error->setText(message); error->show(); });
    connect(player, &app::PlayerApplicationService::soundFontLoadFinished, this,
        [this](bool success) { if (success) accept(); });
    const auto applyTheme = [this](midi_play::settings::ThemeMode mode) {
        const auto& current = theme::themeFor(mode);
        setPalette(theme::widgetPalette(current));
        setStyleSheet(theme::settingsDialogStyle(current));
    };
    applyTheme(themeController ? themeController->mode() : settings->themeMode());
    if (themeController)
        connect(themeController, &theme::ThemeController::themeChanged, this, applyTheme);
    else
        connect(settings, &app::SettingsService::themeModeChanged, this, applyTheme);
    connect(settings, &app::SettingsService::settingsSaveFailed, this,
        [error](const QString& message) { error->setText(message); error->show(); });
}

void SoundFontSetupDialog::reject()
{
    // 音源加载不可取消；等待结束后再继续启动，防止命令行曲目导入与加载重叠。
    if (!m_player->isSoundFontLoading()) QDialog::reject();
}

SoundFontSetup::SoundFontSetup(app::SettingsService* settings, app::PlayerApplicationService* player,
    QWidget* parent, theme::ThemeController* themeController)
    : QObject(parent), m_settings(settings), m_player(player), m_window(parent),
      m_themeController(themeController)
{
    connect(player, &app::PlayerApplicationService::soundFontLoadFinished, this, [this](bool success) {
        if (!m_checkPending) return;
        m_checkPending = false;
        if (success) emit finished();
        else showDialog();
    });
}

void SoundFontSetup::start()
{
    // 先设置一次性门闩，重复调用不会重开对话框或重复检查保存路径。
    if (m_started) return;
    m_started = true;
    if (m_settings->soundFontPath().isEmpty()) {
        showDialog();
        return;
    }
    m_checkPending = true;
    m_player->requestSoundFontLoad(m_settings->soundFontPath());
}

void SoundFontSetup::showDialog()
{
    auto* dialog = new SoundFontSetupDialog(m_settings, m_player, m_window, m_themeController);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    connect(dialog, &QDialog::finished, this, [this] { emit finished(); });
    dialog->open();
}

} // namespace midi_play::presentation::settings
