#include "settingsdialog.h"

#include "app/settingsservice.h"
#include "app/playerapplicationservice.h"
#include "domain/settings/playersettings.h"

#include "presentation/theme/widgetstyles.h"
#include "presentation/theme/themecontroller.h"

#include <QCheckBox>
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
                               QWidget* parent, theme::ThemeController* themeController)
    : QDialog(parent), m_settingsService(settingsService), m_playerService(playerService)
{
    setWindowTitle(QStringLiteral("设置"));
    setWindowFlag(Qt::Window, true);
    setModal(false);
    resize(520, 380);

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

    m_themeCombo = new QComboBox(this);
    m_themeCombo->setObjectName(QStringLiteral("themeModeCombo"));
    m_themeCombo->addItem(QStringLiteral("深色"), int(midi_play::settings::ThemeMode::Dark));
    m_themeCombo->addItem(QStringLiteral("浅色"), int(midi_play::settings::ThemeMode::Light));
    m_themeCombo->setToolTip(QStringLiteral("修改立即生效并自动保存。"));
    form->addRow(QStringLiteral("界面主题"), m_themeCombo);

    m_noteColorCombo = new QComboBox(this);
    m_noteColorCombo->setObjectName(QStringLiteral("noteColorModeCombo"));
    m_noteColorCombo->setAccessibleName(QStringLiteral("音符色彩"));
    m_noteColorCombo->addItem(QStringLiteral("鲜明"),
        midi_play::settings::noteColorModePersistentValue(midi_play::settings::NoteColorMode::Vivid));
    m_noteColorCombo->addItem(QStringLiteral("普通"),
        midi_play::settings::noteColorModePersistentValue(midi_play::settings::NoteColorMode::Normal));
    m_noteColorCombo->setToolTip(QStringLiteral("鲜明模式使用协调的多层次配色；普通模式保持柔和、统一的轨道配色。修改立即生效并自动保存。"));
    form->addRow(QStringLiteral("音符色彩"), m_noteColorCombo);

    m_refreshRateCombo = new QComboBox(this);
    m_refreshRateCombo->setObjectName(QStringLiteral("refreshRateCombo"));
    initializeRefreshRateOptions();
    form->addRow(QStringLiteral("视觉刷新率"), m_refreshRateCombo);

    m_graphicsModeCombo = new QComboBox(this);
    m_graphicsModeCombo->setObjectName(QStringLiteral("graphicsModeCombo"));
    m_graphicsModeCombo->addItem(QStringLiteral("传统 Qt 绘制"),
                                 midi_play::settings::graphicsModePersistentValue(
                                     midi_play::settings::GraphicsMode::Traditional));
#if MIDI_PLAY_HAS_VULKAN
    m_graphicsModeCombo->addItem(QStringLiteral("Vulkan（实验）"),
                                 midi_play::settings::graphicsModePersistentValue(
                                     midi_play::settings::GraphicsMode::VulkanExperimental));
#else
    m_graphicsModeCombo->setEnabled(false);
    m_graphicsModeCombo->setToolTip(QStringLiteral("此版本未包含 Vulkan，当前使用传统 Qt 绘制"));
#endif
    form->addRow(QStringLiteral("图形模式"), m_graphicsModeCombo);

    m_showNotationStripCheckBox = new QCheckBox(QStringLiteral("显示简谱条"), this);
    m_showNotationStripCheckBox->setObjectName(QStringLiteral("showNotationStripCheckBox"));
    m_showNotationStripCheckBox->setChecked(midi_play::settings::kDefaultShowNotationStrip);
    m_showNotationStripCheckBox->setToolTip(QStringLiteral("隐藏时同时隐藏黄线，音符在琴键顶部判定。修改立即生效并自动保存。"));
    form->addRow(QStringLiteral("音符显示"), m_showNotationStripCheckBox);

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

    applyTheme(themeController ? themeController->mode()
        : m_settingsService ? m_settingsService->themeMode() : midi_play::settings::kDefaultThemeMode);

    if (themeController)
        connect(themeController, &theme::ThemeController::themeChanged, this, &SettingsDialog::applyTheme);
    if (m_settingsService) {
        updateThemeSelection(m_settingsService->themeMode());
        connect(m_themeCombo, qOverload<int>(&QComboBox::currentIndexChanged), this, [this] {
            m_errorLabel->hide();
            m_settingsService->setThemeMode(midi_play::settings::themeModeFromPersistentValue(
                m_themeCombo->currentData().toInt()));
        });
        connect(m_settingsService, &app::SettingsService::themeModeChanged,
                this, &SettingsDialog::updateThemeSelection);
        updateNoteColorSelection(m_settingsService->noteColorMode());
        connect(m_noteColorCombo, qOverload<int>(&QComboBox::currentIndexChanged), this, [this] {
            m_errorLabel->hide();
            m_settingsService->setNoteColorMode(
                midi_play::settings::noteColorModeFromPersistentValue(m_noteColorCombo->currentData().toInt()));
        });
        connect(m_settingsService, &app::SettingsService::noteColorModeChanged,
                this, &SettingsDialog::updateNoteColorSelection);
        if (!themeController)
            connect(m_settingsService, &app::SettingsService::themeModeChanged, this, &SettingsDialog::applyTheme);
        m_customRefreshRateSpinBox->setValue(m_settingsService->visualizationRefreshRate());
        updateRefreshRateSelection(m_settingsService->visualizationRefreshRate());
        connect(m_refreshRateCombo, qOverload<int>(&QComboBox::currentIndexChanged),
                this, &SettingsDialog::applyRefreshRateFromUi);
        connect(m_customRefreshRateSpinBox, &QSpinBox::editingFinished,
                this, &SettingsDialog::applyCustomRefreshRateFromUi);
        updateTitleBarModeSelection(m_settingsService->titleBarMode());
        updateGraphicsModeSelection(m_settingsService->graphicsMode());
        updateNotationStripSelection(m_settingsService->showNotationStrip());
        connect(m_showNotationStripCheckBox, &QCheckBox::toggled, this, [this](bool show) {
            m_errorLabel->hide();
            m_settingsService->setShowNotationStrip(show);
        });
        connect(m_settingsService, &app::SettingsService::showNotationStripChanged,
                this, &SettingsDialog::updateNotationStripSelection);
        updateSoundFontPath(m_settingsService->soundFontPath(),
                            m_settingsService->usesDefaultSoundFont());
        connect(m_titleBarModeCombo, qOverload<int>(&QComboBox::currentIndexChanged),
                this, &SettingsDialog::applyTitleBarModeFromUi);
        connect(m_graphicsModeCombo, qOverload<int>(&QComboBox::currentIndexChanged),
                this, &SettingsDialog::applyGraphicsModeFromUi);
        connect(m_loadSoundFontButton, &QPushButton::clicked,
                this, &SettingsDialog::chooseSoundFont);
        connect(m_resetSoundFontButton, &QPushButton::clicked,
                this, &SettingsDialog::resetSoundFont);
        connect(m_settingsService, &app::SettingsService::visualizationRefreshRateChanged,
                this, &SettingsDialog::updateRefreshRateSelection);
        connect(m_settingsService, &app::SettingsService::titleBarModeChanged,
                this, &SettingsDialog::updateTitleBarModeSelection);
        connect(m_settingsService, &app::SettingsService::graphicsModeChanged,
                this, &SettingsDialog::updateGraphicsModeSelection);
        connect(m_settingsService, &app::SettingsService::soundFontPathChanged,
                this, &SettingsDialog::updateSoundFontPath);
        connect(m_settingsService, &app::SettingsService::settingsSaveFailed,
                this, &SettingsDialog::showSaveError);
        connect(m_settingsService, &app::SettingsService::settingsLoadWarning,
                this, &SettingsDialog::showSaveError);
        if (!m_settingsService->lastLoadWarning().isEmpty())
            showSaveError(m_settingsService->lastLoadWarning());
        if (m_playerService) {
            connect(m_playerService, &app::PlayerApplicationService::soundFontLoadFailed,
                    this, &SettingsDialog::showSaveError);
            connect(m_playerService, &app::PlayerApplicationService::soundFontLoadingChanged,
                    this, &SettingsDialog::setSoundFontLoading);
        }
    } else {
        m_themeCombo->setEnabled(false);
        m_noteColorCombo->setEnabled(false);
        m_refreshRateCombo->setEnabled(false);
        m_titleBarModeCombo->setEnabled(false);
        m_graphicsModeCombo->setEnabled(false);
        m_showNotationStripCheckBox->setEnabled(false);
        showSaveError(QStringLiteral("设置服务不可用"));
    }
    if (!m_settingsService || !m_playerService) {
        m_loadSoundFontButton->setEnabled(false);
        m_resetSoundFontButton->setEnabled(false);
    }
    resize(size().expandedTo(sizeHint()));
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

void SettingsDialog::applyGraphicsModeFromUi()
{
    if (!m_settingsService || !m_graphicsModeCombo) return;
    const auto mode = midi_play::settings::graphicsModeFromPersistentValue(
        m_graphicsModeCombo->currentData().toInt());
    m_errorLabel->hide();
    m_settingsService->setGraphicsMode(mode);
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

void SettingsDialog::updateGraphicsModeSelection(midi_play::settings::GraphicsMode mode)
{
    if (!m_graphicsModeCombo) return;
#if !MIDI_PLAY_HAS_VULKAN
    // Display the available backend without overwriting the saved preference.
    mode = midi_play::settings::GraphicsMode::Traditional;
#endif
    const int index = m_graphicsModeCombo->findData(
        midi_play::settings::graphicsModePersistentValue(mode));
    if (index < 0 || index == m_graphicsModeCombo->currentIndex()) return;
    const QSignalBlocker blocker(m_graphicsModeCombo);
    m_graphicsModeCombo->setCurrentIndex(index);
}

void SettingsDialog::applyTheme(midi_play::settings::ThemeMode mode)
{
    const auto& current = theme::themeFor(mode);
    setPalette(theme::widgetPalette(current));
    setStyleSheet(theme::settingsDialogStyle(current));
}

void SettingsDialog::updateThemeSelection(midi_play::settings::ThemeMode mode)
{
    const QSignalBlocker blocker(m_themeCombo);
    m_themeCombo->setCurrentIndex(m_themeCombo->findData(midi_play::settings::themeModePersistentValue(mode)));
}

void SettingsDialog::updateNoteColorSelection(midi_play::settings::NoteColorMode mode)
{
    const QSignalBlocker blocker(m_noteColorCombo);
    m_noteColorCombo->setCurrentIndex(m_noteColorCombo->findData(
        midi_play::settings::noteColorModePersistentValue(mode)));
}

void SettingsDialog::updateNotationStripSelection(bool show)
{
    const QSignalBlocker blocker(m_showNotationStripCheckBox);
    m_showNotationStripCheckBox->setChecked(show);
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
