#pragma once

// 播放器设置窗口：展示并即时修改主题、渲染、窗口装饰、音源、编码器和背景设置。

#include <QDialog>
#include "domain/settings/titlebarmode.h"
#include "domain/settings/graphicsmode.h"
#include "domain/settings/thememode.h"
#include "domain/settings/notecolormode.h"
#include "domain/settings/visualeffectlevel.h"
#include "domain/settings/backgroundimagealignment.h"

class QCheckBox;
class QComboBox;
class QFormLayout;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QSlider;
class QTimer;

namespace midi_play::app { class SettingsService; }
namespace midi_play::app { class PlayerApplicationService; }
namespace midi_play::app { class FfmpegService; }
namespace midi_play::presentation::theme { class ThemeController; }

namespace midi_play::presentation::settings {

// 非模态设置视图；设置服务管理内存状态和保存，音源加载由播放器服务异步执行。
class SettingsDialog final : public QDialog {
    Q_OBJECT
public:
    // 优先复用传入的 FFmpeg 服务；缺省时创建由本窗口管理的检测服务。
    explicit SettingsDialog(app::SettingsService* settingsService,
                            app::PlayerApplicationService* playerService,
                            QWidget* parent = nullptr,
                            theme::ThemeController* themeController = nullptr,
                            app::FfmpegService* ffmpegService = nullptr);

private slots:
    // 选择预设刷新率；数值 0 表示切换到自定义编辑器。
    void applyRefreshRateFromUi();
    // 仅在自定义模式且完整编辑结束后应用刷新率。
    void applyCustomRefreshRateFromUi();
    // 将控件中的持久化枚举值转换为领域标题栏模式。
    void applyTitleBarModeFromUi();
    // 提交图形模式选择，后续视图切换由设置服务信号触发。
    void applyGraphicsModeFromUi();
    // 将关闭选项或特效档位映射到启用开关与级别两个设置字段。
    void applyVisualEffectsFromUi();
    // 复用首次设置的音源选择动作，成功加载后才保存候选路径。
    void chooseSoundFont();
    // 验证图片可读取后保存路径，背景启用开关独立控制。
    void chooseBackgroundImage();
    // 保存手动目录；再次选择同目录时主动重新探测 FFmpeg。
    void chooseFfmpegDirectory();
    // 同步 PATH/手动模式、路径显示和最近检测结果。
    void updateFfmpegControls();
    // 提交背景开关、适配方式和不透明度；控件显隐由模型回填更新。
    void applyBackgroundModeFromUi();
    void applyBackgroundAlignmentFromUi();
    void applyBackgroundOpacityFromUi(int value);
    void scheduleBackgroundMaximumDimensionFromUi(int value);
    void applyBackgroundMaximumDimensionFromUi();
    // 将非预设值映射到自定义项，阻断下拉框信号以避免重复保存。
    void updateRefreshRateSelection(int refreshRate);
    // 模型回填按枚举值查找选项，阻断控件信号以避免双向绑定循环。
    void updateTitleBarModeSelection(midi_play::settings::TitleBarMode mode);
    void updateGraphicsModeSelection(midi_play::settings::GraphicsMode mode);
    void updateNotationStripSelection(bool show);
    void updateThemeSelection(midi_play::settings::ThemeMode mode);
    void updateNoteColorSelection(midi_play::settings::NoteColorMode mode);
    // 更新整个设置窗口的调色板和样式表。
    void applyTheme(midi_play::settings::ThemeMode mode);
    // 回填只读路径并保持文件名可见，完整路径保留在工具提示中。
    void updateSoundFontPath(const QString& path);
    void updateBackgroundImagePath(const QString& path);
    // 同步背景开关和相关行的可见性，按内容重新调整窗口高度。
    void updateBackgroundModeSelection(bool enabled);
    // 阻断输入信号，回填背景适配与规范化的不透明度显示。
    void updateBackgroundAlignmentSelection(midi_play::settings::BackgroundImageAlignment alignment);
    void updateBackgroundOpacitySelection(int opacity);
    void updateBackgroundMaximumDimensionSelection(int dimension);
    // 加载期间禁用重复加载并显示临时状态，结束后等待具体结果信号。
    void setSoundFontLoading(bool loading);
    // 在统一错误区域展示保存、载入或资源验证失败信息。
    void showSaveError(const QString& message);

private:
    // 预设数据为实际 FPS，自定义项使用 0 作为界面哨兵。
    void initializeRefreshRateOptions();
    // 按当前后端重建可选特效列表，构建过程不发布用户编辑信号。
    void rebuildVisualEffectsOptions(bool vulkan);
    // 非 Vulkan 后端关闭已启用特效，再同步选项、当前值与说明。
    void updateVisualEffectsControl(midi_play::settings::GraphicsMode mode);

    // 应用服务为借用指针；仅缺省创建的 FFmpeg 服务通过 Qt 父对象归本窗口管理。
    app::SettingsService* m_settingsService = nullptr;
    app::PlayerApplicationService* m_playerService = nullptr;
    app::FfmpegService* m_ffmpegService = nullptr;
    QComboBox* m_ffmpegModeCombo = nullptr;
    QLineEdit* m_ffmpegPathEdit = nullptr;
    QPushButton* m_chooseFfmpegButton = nullptr;
    QLabel* m_ffmpegStatusLabel = nullptr;
    QComboBox* m_refreshRateCombo = nullptr;
    QComboBox* m_titleBarModeCombo = nullptr;
    QComboBox* m_graphicsModeCombo = nullptr;
    QComboBox* m_themeCombo = nullptr;
    QComboBox* m_noteColorCombo = nullptr;
    QCheckBox* m_showNotationStripCheckBox = nullptr;
    QComboBox* m_visualEffectsCombo = nullptr;
    QLabel* m_customRefreshRateLabel = nullptr;
    QSpinBox* m_customRefreshRateSpinBox = nullptr;
    QLineEdit* m_soundFontPathEdit = nullptr;
    QPushButton* m_loadSoundFontButton = nullptr;
    QLineEdit* m_backgroundImagePathEdit = nullptr;
    QPushButton* m_loadBackgroundImageButton = nullptr;
    QComboBox* m_backgroundModeCombo = nullptr;
    QComboBox* m_backgroundAlignmentCombo = nullptr;
    QSlider* m_backgroundOpacitySlider = nullptr;
    QLabel* m_backgroundOpacityLabel = nullptr;
    QWidget* m_backgroundOpacityEditor = nullptr;
    QSpinBox* m_backgroundMaximumDimensionSpinBox = nullptr;
    QTimer* m_backgroundMaximumDimensionCommitTimer = nullptr;
    QFormLayout* m_formLayout = nullptr;
    QWidget* m_backgroundImageEditor = nullptr;
    QLabel* m_errorLabel = nullptr;
};

} // namespace midi_play::presentation::settings
