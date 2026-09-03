#include "settingsdialog.h"

#include "app/settingsservice.h"
#include "app/playerapplicationservice.h"
#include "domain/settings/playersettings.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QVBoxLayout>

namespace midi_play::presentation::settings {

SettingsDialog::SettingsDialog(app::SettingsService* settingsService,
                               app::PlayerApplicationService* playerService,
                               QWidget* parent)
    : QDialog(parent), m_settingsService(settingsService), m_playerService(playerService)
{
    setWindowTitle(QStringLiteral("设置"));
    setWindowFlag(Qt::Window, true);
    setModal(false);
    resize(520, 340);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(18, 16, 18, 14);
    root->setSpacing(12);

    auto* title = new QLabel(QStringLiteral("播放设置"), this);
    title->setObjectName(QStringLiteral("settingsTitle"));
    root->addWidget(title);

    auto* form = new QFormLayout();
    form->setContentsMargins(0, 0, 0, 0);
    form->setHorizontalSpacing(14);
    form->setVerticalSpacing(10);

    m_refreshRateCombo = new QComboBox(this);
    m_refreshRateCombo->setObjectName(QStringLiteral("refreshRateCombo"));
    initializeRefreshRateOptions();
    form->addRow(QStringLiteral("视觉刷新率"), m_refreshRateCombo);

    m_customRefreshRateLabel = new QLabel(QStringLiteral("自定义刷新率"), this);
    m_customRefreshRateSpinBox = new QSpinBox(this);
    m_customRefreshRateSpinBox->setObjectName(QStringLiteral("customRefreshRateSpinBox"));
    m_customRefreshRateSpinBox->setRange(midi_play::settings::kMinimumVisualizationRefreshRate,
                                         midi_play::settings::kMaximumVisualizationRefreshRate);
    m_customRefreshRateSpinBox->setSuffix(QStringLiteral(" FPS"));
    m_customRefreshRateLabel->setVisible(false);
    m_customRefreshRateSpinBox->setVisible(false);
    form->addRow(m_customRefreshRateLabel, m_customRefreshRateSpinBox);

    m_titleBarModeCombo = new QComboBox(this);
    m_titleBarModeCombo->setObjectName(QStringLiteral("titleBarModeCombo"));
    m_titleBarModeCombo->addItem(QStringLiteral("原生标题栏"),
                                 midi_play::settings::titleBarModePersistentValue(
                                     midi_play::settings::TitleBarMode::Native));
    if (midi_play::settings::isCustomTitleBarAvailable()) {
        m_titleBarModeCombo->addItem(QStringLiteral("自定义标题栏（实验）"),
                                     midi_play::settings::titleBarModePersistentValue(
                                         midi_play::settings::TitleBarMode::Custom));
    } else {
        m_titleBarModeCombo->setToolTip(QStringLiteral("当前平台仅支持原生标题栏"));
    }
    form->addRow(QStringLiteral("标题栏样式"), m_titleBarModeCombo);

    auto* soundFontEditor = new QWidget(this);
    auto* soundFontLayout = new QVBoxLayout(soundFontEditor);
    soundFontLayout->setContentsMargins(0, 0, 0, 0);
    soundFontLayout->setSpacing(6);
    m_soundFontPathEdit = new QLineEdit(soundFontEditor);
    m_soundFontPathEdit->setObjectName(QStringLiteral("soundFontPathEdit"));
    m_soundFontPathEdit->setReadOnly(true);
    m_soundFontPathEdit->setAccessibleName(QStringLiteral("当前音源文件"));
    soundFontLayout->addWidget(m_soundFontPathEdit);
    auto* soundFontActions = new QHBoxLayout();
    soundFontActions->setContentsMargins(0, 0, 0, 0);
    soundFontActions->setSpacing(6);
    m_loadSoundFontButton = new QPushButton(QStringLiteral("加载音源"), soundFontEditor);
    m_loadSoundFontButton->setObjectName(QStringLiteral("loadSoundFontButton"));
    m_loadSoundFontButton->setToolTip(QStringLiteral("选择 SoundFont 音源文件"));
    m_resetSoundFontButton = new QPushButton(QStringLiteral("恢复默认"), soundFontEditor);
    m_resetSoundFontButton->setObjectName(QStringLiteral("resetSoundFontButton"));
    m_resetSoundFontButton->setToolTip(QStringLiteral("恢复随程序提供的默认音源"));
    soundFontActions->addWidget(m_loadSoundFontButton);
    soundFontActions->addWidget(m_resetSoundFontButton);
    soundFontActions->addStretch();
    soundFontLayout->addLayout(soundFontActions);
    form->addRow(QStringLiteral("音源"), soundFontEditor);
    root->addLayout(form);

    auto* hint = new QLabel(QStringLiteral("视觉刷新率仅影响下落音符和界面刷新，不影响音频播放精度。"), this);
    hint->setObjectName(QStringLiteral("settingsHint"));
    hint->setWordWrap(true);
    root->addWidget(hint);

    m_errorLabel = new QLabel(this);
    m_errorLabel->setObjectName(QStringLiteral("settingsError"));
    m_errorLabel->setWordWrap(true);
    m_errorLabel->hide();
    root->addWidget(m_errorLabel);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::close);
    root->addWidget(buttons);

    setStyleSheet(QStringLiteral(R"(
        QDialog { background: #1b1d20; color: #f0f1ed; }
        QLabel { color: #f0f1ed; }
        QLabel#settingsTitle { color: #f0f1ed; font-size: 16px; font-weight: 600; }
        QLabel#settingsHint { color: #aeb4af; font-size: 12px; }
        QLabel#settingsError { color: #ffb4a8; font-size: 12px; }
        QComboBox { min-height: 28px; padding: 2px 8px; background: #25282b; color: #f0f1ed; border: 1px solid #3a3e41; }
        QComboBox:hover { border-color: #555b5f; }
        QSpinBox { min-height: 28px; padding: 2px 8px; background: #25282b; color: #f0f1ed; border: 1px solid #3a3e41; }
        QSpinBox:hover { border-color: #555b5f; }
        QLineEdit { min-height: 28px; padding: 2px 8px; background: #202326; color: #d8dbd7; border: 1px solid #3a3e41; }
        QLineEdit:read-only { color: #bfc3bf; }
        QPushButton { min-width: 72px; min-height: 28px; color: #dfe1dc; background: #25282b; border: 1px solid #3a3e41; }
        QPushButton:hover { background: #2d3033; }
        QPushButton:disabled { color: #676c68; background: #202326; border-color: #303337; }
    )"));

    if (m_settingsService) {
        m_customRefreshRateSpinBox->setValue(m_settingsService->visualizationRefreshRate());
        updateRefreshRateSelection(m_settingsService->visualizationRefreshRate());
        connect(m_refreshRateCombo, qOverload<int>(&QComboBox::currentIndexChanged),
                this, &SettingsDialog::applyRefreshRateFromUi);
        connect(m_customRefreshRateSpinBox, &QSpinBox::editingFinished,
                this, &SettingsDialog::applyCustomRefreshRateFromUi);
        updateTitleBarModeSelection(m_settingsService->titleBarMode());
        updateSoundFontPath(m_settingsService->soundFontPath(),
                            m_settingsService->usesDefaultSoundFont());
        connect(m_titleBarModeCombo, qOverload<int>(&QComboBox::currentIndexChanged),
                this, &SettingsDialog::applyTitleBarModeFromUi);
        connect(m_loadSoundFontButton, &QPushButton::clicked,
                this, &SettingsDialog::chooseSoundFont);
        connect(m_resetSoundFontButton, &QPushButton::clicked,
                this, &SettingsDialog::resetSoundFont);
        connect(m_settingsService, &app::SettingsService::visualizationRefreshRateChanged,
                this, &SettingsDialog::updateRefreshRateSelection);
        connect(m_settingsService, &app::SettingsService::titleBarModeChanged,
                this, &SettingsDialog::updateTitleBarModeSelection);
        connect(m_settingsService, &app::SettingsService::soundFontPathChanged,
                this, &SettingsDialog::updateSoundFontPath);
        connect(m_settingsService, &app::SettingsService::settingsSaveFailed,
                this, &SettingsDialog::showSaveError);
        if (m_playerService) {
            connect(m_playerService, &app::PlayerApplicationService::soundFontLoadFailed,
                    this, &SettingsDialog::showSaveError);
            connect(m_playerService, &app::PlayerApplicationService::soundFontLoadingChanged,
                    this, &SettingsDialog::setSoundFontLoading);
        }
    } else {
        m_refreshRateCombo->setEnabled(false);
        m_titleBarModeCombo->setEnabled(false);
        showSaveError(QStringLiteral("设置服务不可用"));
    }
    if (!m_settingsService || !m_playerService) {
        m_loadSoundFontButton->setEnabled(false);
        m_resetSoundFontButton->setEnabled(false);
    }
}

void SettingsDialog::applyRefreshRateFromUi()
{
    if (!m_settingsService || !m_refreshRateCombo) {
        return;
    }

    const int refreshRate = m_refreshRateCombo->currentData().toInt();
    if (refreshRate == 0) {
        m_customRefreshRateLabel->setVisible(true);
        m_customRefreshRateSpinBox->setVisible(true);
        applyCustomRefreshRateFromUi();
        return;
    }
    m_customRefreshRateLabel->setVisible(false);
    m_customRefreshRateSpinBox->setVisible(false);
    m_errorLabel->hide();
    m_settingsService->setVisualizationRefreshRate(refreshRate);
}

void SettingsDialog::applyCustomRefreshRateFromUi()
{
    if (!m_settingsService || !m_refreshRateCombo || !m_customRefreshRateSpinBox
        || m_refreshRateCombo->currentData().toInt() != 0) {
        return;
    }

    m_errorLabel->hide();
    m_settingsService->setVisualizationRefreshRate(m_customRefreshRateSpinBox->value());
}

void SettingsDialog::applyTitleBarModeFromUi()
{
    if (!m_settingsService || !m_titleBarModeCombo) {
        return;
    }

    const int value = m_titleBarModeCombo->currentData().toInt();
    m_errorLabel->hide();
    m_settingsService->setTitleBarMode(
        midi_play::settings::titleBarModeFromPersistentValue(value));
}

void SettingsDialog::chooseSoundFont()
{
    if (!m_settingsService || !m_playerService) {
        return;
    }

    const QString currentPath = m_settingsService->soundFontPath();
    const QString initialDirectory = QFileInfo(currentPath).absolutePath();
    const QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("加载音源"), initialDirectory,
        QStringLiteral("SoundFont 音源 (*.sf2 *.sf3)"));
    if (!path.isEmpty()) {
        m_errorLabel->hide();
        m_playerService->requestSoundFontLoad(path);
    }
}

void SettingsDialog::resetSoundFont()
{
    if (!m_settingsService || !m_playerService) {
        return;
    }

    m_errorLabel->hide();
    m_playerService->requestSoundFontLoad(m_settingsService->defaultSoundFontPath());
}

void SettingsDialog::updateRefreshRateSelection(int refreshRate)
{
    if (!m_refreshRateCombo) {
        return;
    }

    int index = m_refreshRateCombo->findData(refreshRate);
    const bool custom = index < 0;
    if (custom) index = m_refreshRateCombo->findData(0);
    if (index < 0) return;

    const QSignalBlocker blocker(m_refreshRateCombo);
    m_refreshRateCombo->setCurrentIndex(index);
    m_customRefreshRateLabel->setVisible(custom);
    m_customRefreshRateSpinBox->setVisible(custom);
    if (custom) {
        m_customRefreshRateSpinBox->setValue(refreshRate);
    }
}

void SettingsDialog::updateTitleBarModeSelection(midi_play::settings::TitleBarMode mode)
{
    if (!m_titleBarModeCombo) {
        return;
    }

    const int index = m_titleBarModeCombo->findData(
        midi_play::settings::titleBarModePersistentValue(mode));
    if (index < 0 || index == m_titleBarModeCombo->currentIndex()) {
        return;
    }

    const QSignalBlocker blocker(m_titleBarModeCombo);
    m_titleBarModeCombo->setCurrentIndex(index);
}

void SettingsDialog::updateSoundFontPath(const QString& path, bool usesDefault)
{
    if (!m_soundFontPathEdit || !m_resetSoundFontButton) {
        return;
    }

    m_soundFontPathEdit->setText(path);
    m_soundFontPathEdit->setToolTip(usesDefault
        ? QStringLiteral("默认音源：%1").arg(path)
        : QStringLiteral("自定义音源：%1").arg(path));
    m_soundFontPathEdit->setAccessibleDescription(
        usesDefault ? QStringLiteral("默认音源") : QStringLiteral("自定义音源"));
    // Keep the file name visible when the absolute path is wider than the editor.
    m_soundFontPathEdit->setCursorPosition(path.size());
    m_resetSoundFontButton->setEnabled(!usesDefault && !m_soundFontLoading);
}

void SettingsDialog::setSoundFontLoading(bool loading)
{
    m_soundFontLoading = loading;
    m_loadSoundFontButton->setEnabled(!loading);
    m_resetSoundFontButton->setEnabled(!loading && m_settingsService
                                       && !m_settingsService->usesDefaultSoundFont());
    if (loading) {
        m_errorLabel->setText(QStringLiteral("正在加载音源..."));
        m_errorLabel->show();
    } else {
        // The loading message is transient. A failed request emits its real
        // error immediately after this state transition, while a successful
        // request leaves the settings page clean.
        m_errorLabel->clear();
        m_errorLabel->hide();
    }
}

void SettingsDialog::showSaveError(const QString& message)
{
    if (!m_errorLabel) {
        return;
    }

    m_errorLabel->setText(message);
    m_errorLabel->show();
}

void SettingsDialog::initializeRefreshRateOptions()
{
    m_refreshRateCombo->addItem(QStringLiteral("30 FPS"), 30);
    m_refreshRateCombo->addItem(QStringLiteral("60 FPS"), 60);
    m_refreshRateCombo->addItem(QStringLiteral("120 FPS"), 120);
    m_refreshRateCombo->addItem(QStringLiteral("自定义..."), 0);
}

} // namespace midi_play::presentation::settings
