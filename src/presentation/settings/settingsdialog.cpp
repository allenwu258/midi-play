#include "settingsdialog.h"
#include "soundfontsetup.h"

#include "app/settingsservice.h"
#include "app/ffmpegservice.h"
#include "app/playerapplicationservice.h"
#include "domain/settings/playersettings.h"

#include "presentation/theme/widgetstyles.h"
#include "presentation/theme/themecontroller.h"
#include "presentation/visualization/backgroundimageloader.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>
#include <QVBoxLayout>

#include <algorithm>

namespace midi_play::presentation::settings {
namespace {

constexpr int kVisualEffectsDisabledUiValue = -1;

} // namespace

SettingsDialog::SettingsDialog(app::SettingsService* settingsService,
                               app::PlayerApplicationService* playerService,
                               QWidget* parent, theme::ThemeController* themeController,
                               app::FfmpegService* ffmpegService)
    : QDialog(parent), m_settingsService(settingsService), m_playerService(playerService)
{
    m_ffmpegService = ffmpegService ? ffmpegService : new app::FfmpegService(settingsService, this);
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
    m_formLayout = form;
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
    m_graphicsModeCombo->addItem(QStringLiteral("传统 Qt 绘制（兼容）"),
                                 midi_play::settings::graphicsModePersistentValue(
                                     midi_play::settings::GraphicsMode::Traditional));
#if MIDI_PLAY_HAS_VULKAN
    m_graphicsModeCombo->addItem(QStringLiteral("Vulkan（推荐）"),
                                 midi_play::settings::graphicsModePersistentValue(
                                     midi_play::settings::GraphicsMode::VulkanExperimental));
#else
    m_graphicsModeCombo->setEnabled(false);
    m_graphicsModeCombo->setToolTip(QStringLiteral("此版本未包含 Vulkan，当前使用传统 Qt 绘制"));
#endif
    form->addRow(QStringLiteral("图形模式"), m_graphicsModeCombo);

    m_visualEffectsCombo = new QComboBox(this);
    m_visualEffectsCombo->setObjectName(QStringLiteral("visualEffectsCombo"));
    m_visualEffectsCombo->setAccessibleName(QStringLiteral("流光特效"));
    m_visualEffectsCombo->addItem(QStringLiteral("无特效（特效需开启 Vulkan）"),
                                  kVisualEffectsDisabledUiValue);
    form->addRow(QStringLiteral("流光特效"), m_visualEffectsCombo);

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
    m_soundFontPathEdit->setPlaceholderText(QStringLiteral("尚未配置音源，请选择 SF2/SF3 文件"));
    m_soundFontPathEdit->setAccessibleName(QStringLiteral("当前音源文件"));
    soundFontLayout->addWidget(m_soundFontPathEdit);
    auto* soundFontActions = new QHBoxLayout();
    soundFontActions->setContentsMargins(0, 0, 0, 0);
    soundFontActions->setSpacing(6);
    m_loadSoundFontButton = new QPushButton(QStringLiteral("加载音源"), soundFontEditor);
    m_loadSoundFontButton->setObjectName(QStringLiteral("loadSoundFontButton"));
    m_loadSoundFontButton->setToolTip(QStringLiteral("选择 SoundFont 音源文件"));
    soundFontActions->addWidget(m_loadSoundFontButton);
    soundFontActions->addStretch();
    soundFontLayout->addLayout(soundFontActions);
    form->addRow(QStringLiteral("音源"), soundFontEditor);

    auto* ffmpegEditor = new QWidget(this);
    auto* ffmpegLayout = new QVBoxLayout(ffmpegEditor);
    ffmpegLayout->setContentsMargins(0, 0, 0, 0);
    ffmpegLayout->setSpacing(6);
    m_ffmpegModeCombo = new QComboBox(ffmpegEditor);
    m_ffmpegModeCombo->setObjectName(QStringLiteral("ffmpegModeCombo"));
    m_ffmpegModeCombo->addItem(QStringLiteral("自动读取 PATH"), true);
    m_ffmpegModeCombo->addItem(QStringLiteral("手动选择目录"), false);
    ffmpegLayout->addWidget(m_ffmpegModeCombo);
    auto* ffmpegPathRow = new QHBoxLayout();
    m_ffmpegPathEdit = new QLineEdit(ffmpegEditor);
    m_ffmpegPathEdit->setObjectName(QStringLiteral("ffmpegPathEdit"));
    m_ffmpegPathEdit->setReadOnly(true);
    m_ffmpegPathEdit->setPlaceholderText(QStringLiteral("尚未选择 FFmpeg 目录"));
    m_chooseFfmpegButton = new QPushButton(QStringLiteral("选择目录"), ffmpegEditor);
    m_chooseFfmpegButton->setObjectName(QStringLiteral("chooseFfmpegButton"));
    ffmpegPathRow->addWidget(m_ffmpegPathEdit, 1);
    ffmpegPathRow->addWidget(m_chooseFfmpegButton);
    ffmpegLayout->addLayout(ffmpegPathRow);
    m_ffmpegStatusLabel = new QLabel(ffmpegEditor);
    m_ffmpegStatusLabel->setObjectName(QStringLiteral("ffmpegStatusLabel"));
    m_ffmpegStatusLabel->setTextFormat(Qt::PlainText);
    m_ffmpegStatusLabel->setWordWrap(false);
    ffmpegLayout->addWidget(m_ffmpegStatusLabel);
    form->addRow(QStringLiteral("视频编码器"), ffmpegEditor);
    connect(m_ffmpegService, &app::FfmpegService::changed, this, &SettingsDialog::updateFfmpegControls);
    connect(m_chooseFfmpegButton, &QPushButton::clicked, this, &SettingsDialog::chooseFfmpegDirectory);
    connect(m_ffmpegModeCombo, &QComboBox::currentIndexChanged, this, [this] {
        if (m_settingsService) m_settingsService->setFfmpegUsePath(m_ffmpegModeCombo->currentData().toBool());
    });
    if (m_settingsService) {
        connect(m_settingsService, &app::SettingsService::ffmpegPathChanged, this, &SettingsDialog::updateFfmpegControls);
        connect(m_settingsService, &app::SettingsService::ffmpegUsePathChanged, this, &SettingsDialog::updateFfmpegControls);
    }
    updateFfmpegControls();

    m_backgroundModeCombo = new QComboBox(this);
    m_backgroundModeCombo->setObjectName(QStringLiteral("backgroundModeCombo"));
    m_backgroundModeCombo->addItem(QStringLiteral("无背景"), false);
    m_backgroundModeCombo->addItem(QStringLiteral("图片背景（实验）"), true);
    form->addRow(QStringLiteral("下落背景"), m_backgroundModeCombo);

    auto* backgroundEditor = new QWidget(this);
    m_backgroundImageEditor = backgroundEditor;
    auto* backgroundLayout = new QVBoxLayout(backgroundEditor);
    backgroundLayout->setContentsMargins(0, 0, 0, 0);
    backgroundLayout->setSpacing(6);
    m_backgroundImagePathEdit = new QLineEdit(backgroundEditor);
    m_backgroundImagePathEdit->setObjectName(QStringLiteral("backgroundImagePathEdit"));
    m_backgroundImagePathEdit->setReadOnly(true);
    m_backgroundImagePathEdit->setPlaceholderText(QStringLiteral("尚未选择图片"));
    m_backgroundImagePathEdit->setAccessibleName(QStringLiteral("下落音符背景图片"));
    backgroundLayout->addWidget(m_backgroundImagePathEdit);
    auto* backgroundActions = new QHBoxLayout();
    backgroundActions->setContentsMargins(0, 0, 0, 0);
    backgroundActions->setSpacing(6);
    m_loadBackgroundImageButton = new QPushButton(QStringLiteral("选择图片"), backgroundEditor);
    m_loadBackgroundImageButton->setObjectName(QStringLiteral("loadBackgroundImageButton"));
    m_loadBackgroundImageButton->setToolTip(QStringLiteral("选择本地 PNG、JPEG、WebP 或 BMP 图片"));
    backgroundActions->addWidget(m_loadBackgroundImageButton);
    backgroundActions->addStretch();
    backgroundLayout->addLayout(backgroundActions);
    form->addRow(QStringLiteral("背景图片"), backgroundEditor);
    form->setRowVisible(backgroundEditor, false);

    m_backgroundAlignmentCombo = new QComboBox(this);
    m_backgroundAlignmentCombo->setObjectName(QStringLiteral("backgroundAlignmentCombo"));
    m_backgroundAlignmentCombo->setAccessibleName(QStringLiteral("背景适配"));
    m_backgroundAlignmentCombo->setToolTip(QStringLiteral(
        "顶部、底部、左侧和右侧对齐会锚定裁剪位置；裁剪铺满居中裁剪，完整显示保留整张图片。"));
    m_backgroundAlignmentCombo->addItem(QStringLiteral("顶部对齐"),
        int(midi_play::settings::BackgroundImageAlignment::Top));
    m_backgroundAlignmentCombo->addItem(QStringLiteral("底部对齐"),
        int(midi_play::settings::BackgroundImageAlignment::Bottom));
    m_backgroundAlignmentCombo->addItem(QStringLiteral("左侧对齐"),
        int(midi_play::settings::BackgroundImageAlignment::Left));
    m_backgroundAlignmentCombo->addItem(QStringLiteral("右侧对齐"),
        int(midi_play::settings::BackgroundImageAlignment::Right));
    m_backgroundAlignmentCombo->addItem(QStringLiteral("裁剪铺满"),
        int(midi_play::settings::BackgroundImageAlignment::Cover));
    m_backgroundAlignmentCombo->addItem(QStringLiteral("完整显示"),
        int(midi_play::settings::BackgroundImageAlignment::Contain));
    form->addRow(QStringLiteral("背景适配"), m_backgroundAlignmentCombo);

    m_backgroundOpacityEditor = new QWidget(this);
    auto* opacityLayout = new QHBoxLayout(m_backgroundOpacityEditor);
    opacityLayout->setContentsMargins(0, 0, 0, 0);
    opacityLayout->setSpacing(8);
    m_backgroundOpacitySlider = new QSlider(Qt::Horizontal, m_backgroundOpacityEditor);
    m_backgroundOpacitySlider->setObjectName(QStringLiteral("backgroundOpacitySlider"));
    m_backgroundOpacitySlider->setRange(0, 100);
    m_backgroundOpacitySlider->setSingleStep(5);
    m_backgroundOpacitySlider->setPageStep(10);
    m_backgroundOpacitySlider->setAccessibleName(QStringLiteral("背景不透明度"));
    m_backgroundOpacityLabel = new QLabel(QStringLiteral("100%"), m_backgroundOpacityEditor);
    m_backgroundOpacityLabel->setObjectName(QStringLiteral("backgroundOpacityLabel"));
    m_backgroundOpacityLabel->setMinimumWidth(42);
    m_backgroundOpacityLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    opacityLayout->addWidget(m_backgroundOpacitySlider, 1);
    opacityLayout->addWidget(m_backgroundOpacityLabel);
    form->addRow(QStringLiteral("背景不透明度"), m_backgroundOpacityEditor);
    form->setRowVisible(m_backgroundAlignmentCombo, false);
    form->setRowVisible(m_backgroundOpacityEditor, false);
    root->addLayout(form);

    auto* hint = new QLabel(QStringLiteral("视觉刷新率仅影响下落音符和界面刷新，不影响音频播放精度；背景图片只覆盖下落音符区域，底部键盘会连续延伸到左侧。"), this);
    hint->setObjectName(QStringLiteral("settingsHint"));
    hint->setWordWrap(true);
    root->addWidget(hint);

    m_errorLabel = new QLabel(this);
    m_errorLabel->setObjectName(QStringLiteral("settingsError"));
    m_errorLabel->setTextFormat(Qt::PlainText);
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
        updateVisualEffectsControl(m_settingsService->graphicsMode());
        updateNotationStripSelection(m_settingsService->showNotationStrip());
        connect(m_visualEffectsCombo, qOverload<int>(&QComboBox::currentIndexChanged),
                this, &SettingsDialog::applyVisualEffectsFromUi);
        connect(m_settingsService, &app::SettingsService::visualEffectsEnabledChanged,
                this, [this](bool) {
            m_errorLabel->hide();
            updateVisualEffectsControl(m_settingsService->graphicsMode());
        });
        connect(m_settingsService, &app::SettingsService::visualEffectsLevelChanged,
                this, [this](midi_play::settings::VisualEffectLevel) {
            updateVisualEffectsControl(m_settingsService->graphicsMode());
        });
        connect(m_showNotationStripCheckBox, &QCheckBox::toggled, this, [this](bool show) {
            m_errorLabel->hide();
            m_settingsService->setShowNotationStrip(show);
        });
        connect(m_settingsService, &app::SettingsService::showNotationStripChanged,
                this, &SettingsDialog::updateNotationStripSelection);
        updateSoundFontPath(m_settingsService->soundFontPath());
        updateBackgroundImagePath(m_settingsService->backgroundImagePath());
        updateBackgroundModeSelection(m_settingsService->backgroundImageEnabled());
        updateBackgroundAlignmentSelection(m_settingsService->backgroundImageAlignment());
        updateBackgroundOpacitySelection(m_settingsService->backgroundImageOpacity());
        connect(m_titleBarModeCombo, qOverload<int>(&QComboBox::currentIndexChanged),
                this, &SettingsDialog::applyTitleBarModeFromUi);
        connect(m_graphicsModeCombo, qOverload<int>(&QComboBox::currentIndexChanged),
                this, &SettingsDialog::applyGraphicsModeFromUi);
        connect(m_loadSoundFontButton, &QPushButton::clicked,
                this, &SettingsDialog::chooseSoundFont);
        connect(m_loadBackgroundImageButton, &QPushButton::clicked,
                this, &SettingsDialog::chooseBackgroundImage);
        connect(m_backgroundModeCombo, qOverload<int>(&QComboBox::currentIndexChanged),
                this, &SettingsDialog::applyBackgroundModeFromUi);
        connect(m_backgroundAlignmentCombo, qOverload<int>(&QComboBox::currentIndexChanged),
                this, &SettingsDialog::applyBackgroundAlignmentFromUi);
        connect(m_backgroundOpacitySlider, &QSlider::valueChanged,
                this, &SettingsDialog::applyBackgroundOpacityFromUi);
        connect(m_settingsService, &app::SettingsService::visualizationRefreshRateChanged,
                this, &SettingsDialog::updateRefreshRateSelection);
        connect(m_settingsService, &app::SettingsService::titleBarModeChanged,
                this, &SettingsDialog::updateTitleBarModeSelection);
        connect(m_settingsService, &app::SettingsService::graphicsModeChanged,
                this, [this](midi_play::settings::GraphicsMode mode) {
            updateGraphicsModeSelection(mode);
            updateVisualEffectsControl(mode);
        });
        connect(m_settingsService, &app::SettingsService::soundFontPathChanged,
                this, &SettingsDialog::updateSoundFontPath);
        connect(m_settingsService, &app::SettingsService::backgroundImagePathChanged,
                this, &SettingsDialog::updateBackgroundImagePath);
        connect(m_settingsService, &app::SettingsService::backgroundImageEnabledChanged,
                this, &SettingsDialog::updateBackgroundModeSelection);
        connect(m_settingsService, &app::SettingsService::backgroundImageAlignmentChanged,
                this, &SettingsDialog::updateBackgroundAlignmentSelection);
        connect(m_settingsService, &app::SettingsService::backgroundImageOpacityChanged,
                this, &SettingsDialog::updateBackgroundOpacitySelection);
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
            if (m_playerService->isSoundFontLoading()) setSoundFontLoading(true);
            else if (!m_playerService->lastSoundFontError().isEmpty())
                showSaveError(m_playerService->lastSoundFontError());
        }
    } else {
        m_themeCombo->setEnabled(false);
        m_noteColorCombo->setEnabled(false);
        m_refreshRateCombo->setEnabled(false);
        m_titleBarModeCombo->setEnabled(false);
        m_graphicsModeCombo->setEnabled(false);
        m_visualEffectsCombo->setEnabled(false);
        m_showNotationStripCheckBox->setEnabled(false);
        m_backgroundModeCombo->setEnabled(false);
        m_loadBackgroundImageButton->setEnabled(false);
        m_backgroundAlignmentCombo->setEnabled(false);
        m_backgroundOpacitySlider->setEnabled(false);
        showSaveError(QStringLiteral("设置服务不可用"));
    }
    if (!m_settingsService || !m_playerService) {
        m_loadSoundFontButton->setEnabled(false);
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

void SettingsDialog::applyVisualEffectsFromUi()
{
    if (!m_settingsService || !m_visualEffectsCombo) return;
    m_errorLabel->hide();
    const int value = m_visualEffectsCombo->currentData().toInt();
    if (value == kVisualEffectsDisabledUiValue) {
        m_settingsService->setVisualEffectsEnabled(false);
        return;
    }
    m_settingsService->setVisualEffectsLevel(
        midi_play::settings::visualEffectLevelFromPersistentValue(value));
    m_settingsService->setVisualEffectsEnabled(true);
}

void SettingsDialog::chooseSoundFont()
{
    chooseSoundFontFile(this, m_settingsService, m_playerService);
}

void SettingsDialog::chooseFfmpegDirectory()
{
    if (!m_settingsService) return;
    const QString directory = QFileDialog::getExistingDirectory(this, QStringLiteral("选择 FFmpeg 目录"),
                                                                 m_settingsService->ffmpegPath());
    if (directory.isEmpty()) return;
    const bool unchanged = directory == m_settingsService->ffmpegPath();
    m_settingsService->setFfmpegPath(directory);
    if (unchanged) m_ffmpegService->refresh();
}

void SettingsDialog::updateFfmpegControls()
{
    const bool usePath = !m_settingsService || m_settingsService->ffmpegUsePath();
    const QSignalBlocker blocker(m_ffmpegModeCombo);
    m_ffmpegModeCombo->setCurrentIndex(m_ffmpegModeCombo->findData(usePath));
    m_ffmpegModeCombo->setEnabled(m_settingsService);
    m_ffmpegPathEdit->setVisible(!usePath);
    m_chooseFfmpegButton->setVisible(!usePath);
    m_chooseFfmpegButton->setEnabled(m_settingsService);
    m_ffmpegPathEdit->setText(m_settingsService ? m_settingsService->ffmpegPath() : QString());
    m_ffmpegPathEdit->setToolTip(m_ffmpegPathEdit->text());
    const auto& result = m_ffmpegService->result();
    m_ffmpegStatusLabel->setText(result.valid ? QStringLiteral("FFmpeg 可用") : m_ffmpegService->unavailableReason());
    m_ffmpegStatusLabel->setToolTip(result.valid ? result.executablePath : result.error);
}

void SettingsDialog::chooseBackgroundImage()
{
    if (!m_settingsService) return;
    const QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("选择下落音符背景图片"), {},
        QStringLiteral("图片文件 (*.png *.jpg *.jpeg *.webp *.bmp);;所有文件 (*)"));
    if (path.isEmpty()) return;
    QString error;
    if (!visualization::canReadBackgroundImage(path, &error)) {
        showSaveError(QStringLiteral("无法读取背景图片：%1").arg(error));
        return;
    }
    m_errorLabel->hide();
    m_settingsService->setBackgroundImagePath(path);
}

void SettingsDialog::applyBackgroundModeFromUi()
{
    if (!m_settingsService) return;
    m_errorLabel->hide();
    m_settingsService->setBackgroundImageEnabled(m_backgroundModeCombo->currentData().toBool());
}

void SettingsDialog::applyBackgroundAlignmentFromUi()
{
    if (!m_settingsService) return;
    m_errorLabel->hide();
    m_settingsService->setBackgroundImageAlignment(
        midi_play::settings::backgroundImageAlignmentFromPersistentValue(
            m_backgroundAlignmentCombo->currentData().toInt()));
}

void SettingsDialog::applyBackgroundOpacityFromUi(int value)
{
    if (!m_settingsService) return;
    m_errorLabel->hide();
    m_settingsService->setBackgroundImageOpacity(value);
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

void SettingsDialog::rebuildVisualEffectsOptions(bool vulkan)
{
    if (!m_visualEffectsCombo) return;

    const QSignalBlocker blocker(m_visualEffectsCombo);
    m_visualEffectsCombo->clear();
    if (!vulkan) {
        m_visualEffectsCombo->addItem(QStringLiteral("无特效（特效需开启 Vulkan）"),
                                      kVisualEffectsDisabledUiValue);
        return;
    }

    m_visualEffectsCombo->addItem(QStringLiteral("无特效"), kVisualEffectsDisabledUiValue);
    m_visualEffectsCombo->addItem(QStringLiteral("流光玻璃（低）"),
        midi_play::settings::visualEffectLevelPersistentValue(
            midi_play::settings::VisualEffectLevel::Low));
    m_visualEffectsCombo->addItem(QStringLiteral("流光玻璃（中）"),
        midi_play::settings::visualEffectLevelPersistentValue(
            midi_play::settings::VisualEffectLevel::Medium));
    m_visualEffectsCombo->addItem(QStringLiteral("流光玻璃（高）"),
        midi_play::settings::visualEffectLevelPersistentValue(
            midi_play::settings::VisualEffectLevel::High));
}

void SettingsDialog::updateVisualEffectsControl(midi_play::settings::GraphicsMode mode)
{
    if (!m_visualEffectsCombo) return;

#if MIDI_PLAY_HAS_VULKAN
    const bool vulkan = midi_play::settings::normalizeGraphicsMode(mode)
        == midi_play::settings::GraphicsMode::VulkanExperimental;
#else
    Q_UNUSED(mode)
    const bool vulkan = false;
#endif

    if (!vulkan && m_settingsService && m_settingsService->visualEffectsEnabled())
        m_settingsService->setVisualEffectsEnabled(false);
    rebuildVisualEffectsOptions(vulkan);
    const bool enabled = vulkan && m_settingsService && m_settingsService->visualEffectsEnabled();
    const int target = enabled
        ? m_visualEffectsCombo->findData(
            midi_play::settings::visualEffectLevelPersistentValue(
                m_settingsService->visualEffectsLevel()))
        : m_visualEffectsCombo->findData(kVisualEffectsDisabledUiValue);
    if (target >= 0 && target != m_visualEffectsCombo->currentIndex()) {
        const QSignalBlocker blocker(m_visualEffectsCombo);
        m_visualEffectsCombo->setCurrentIndex(target);
    }
    m_visualEffectsCombo->setToolTip(vulkan
        ? QString()
        : QStringLiteral("需要启用 Vulkan 才能使用流光特效"));
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

void SettingsDialog::updateSoundFontPath(const QString& path)
{
    m_soundFontPathEdit->setText(path);
    m_soundFontPathEdit->setToolTip(path.isEmpty() ? QStringLiteral("尚未配置音源") : path);
    m_soundFontPathEdit->setAccessibleDescription(QStringLiteral("用户选择的音源文件"));
    // Keep the file name visible when the absolute path is wider than the editor.
    m_soundFontPathEdit->setCursorPosition(path.size());
}

void SettingsDialog::updateBackgroundImagePath(const QString& path)
{
    if (!m_backgroundImagePathEdit) return;
    m_backgroundImagePathEdit->setText(path);
    m_backgroundImagePathEdit->setToolTip(path.isEmpty() ? QStringLiteral("尚未选择图片") : path);
    m_backgroundImagePathEdit->setCursorPosition(path.size());
}

void SettingsDialog::updateBackgroundModeSelection(bool enabled)
{
    const QSignalBlocker blocker(m_backgroundModeCombo);
    m_backgroundModeCombo->setCurrentIndex(m_backgroundModeCombo->findData(enabled));
    m_formLayout->setRowVisible(m_backgroundImageEditor, enabled);
    m_formLayout->setRowVisible(m_backgroundAlignmentCombo, enabled);
    m_formLayout->setRowVisible(m_backgroundOpacityEditor, enabled);
    if (isVisible()) resize(width(), sizeHint().height());
}

void SettingsDialog::updateBackgroundAlignmentSelection(
    midi_play::settings::BackgroundImageAlignment alignment)
{
    if (!m_backgroundAlignmentCombo) return;
    const QSignalBlocker blocker(m_backgroundAlignmentCombo);
    m_backgroundAlignmentCombo->setCurrentIndex(m_backgroundAlignmentCombo->findData(
        midi_play::settings::backgroundImageAlignmentPersistentValue(alignment)));
}

void SettingsDialog::updateBackgroundOpacitySelection(int opacity)
{
    if (!m_backgroundOpacitySlider) return;
    const int normalized = std::clamp(opacity, 0, 100);
    const QSignalBlocker blocker(m_backgroundOpacitySlider);
    m_backgroundOpacitySlider->setValue(normalized);
    m_backgroundOpacityLabel->setText(QStringLiteral("%1%").arg(normalized));
    m_backgroundOpacitySlider->setToolTip(QStringLiteral("100% 为完全显示，修改立即生效并自动保存。"));
}

void SettingsDialog::setSoundFontLoading(bool loading)
{
    m_loadSoundFontButton->setEnabled(!loading);
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
