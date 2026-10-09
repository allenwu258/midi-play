#pragma once
// 用户设置应用服务：规范化快照、广播字段变更，并委托存储端口持久化。

#include "domain/settings/playersettings.h"
#include "isettingsstore.h"

#include <QObject>
#include <QString>

#include <memory>

namespace midi_play::app {

// GUI 线程中的设置所有者；变更先应用并通知，保存失败不回滚当前内存值。
class SettingsService final : public QObject {
    Q_OBJECT
public:
    // 接管存储实现并注册跨 Qt 信号连接使用的设置枚举。
    explicit SettingsService(std::unique_ptr<ISettingsStore> store, QObject* parent = nullptr);

    // 当前规范化快照及各字段查询；引用随本服务的设置更新而变化。
    const settings::PlayerSettings& settings() const noexcept { return m_settings; }
    int visualizationRefreshRate() const noexcept { return m_settings.visualizationRefreshRate; }
    settings::GraphicsMode graphicsMode() const noexcept { return m_settings.graphicsMode; }
    bool graphicsModeConfigured() const noexcept { return m_settings.graphicsModeConfigured; }
    bool showNotationStrip() const noexcept { return m_settings.showNotationStrip; }
    settings::ThemeMode themeMode() const noexcept { return m_settings.themeMode; }
    settings::NoteColorMode noteColorMode() const noexcept { return m_settings.noteColorMode; }
    const QString& lastLoadWarning() const noexcept { return m_lastLoadWarning; }
    settings::TitleBarMode titleBarMode() const noexcept { return m_settings.titleBarMode; }
    const QString& soundFontPath() const noexcept { return m_settings.soundFontPath; }
    const QString& ffmpegPath() const noexcept { return m_settings.ffmpegPath; }
    bool ffmpegUsePath() const noexcept { return m_settings.ffmpegUsePath; }
    const QString& backgroundImagePath() const noexcept { return m_settings.backgroundImagePath; }
    bool backgroundImageEnabled() const noexcept { return m_settings.backgroundImageEnabled; }
    settings::BackgroundImageAlignment backgroundImageAlignment() const noexcept
    { return m_settings.backgroundImageAlignment; }
    int backgroundImageOpacity() const noexcept { return m_settings.backgroundImageOpacity; }
    bool visualEffectsEnabled() const noexcept { return m_settings.visualEffectsEnabled; }
    settings::VisualEffectLevel visualEffectsLevel() const noexcept { return m_settings.visualEffectsLevel; }
    // 仅背景启用时返回路径，关闭时保留持久化路径但向渲染器返回空串。
    QString activeBackgroundImagePath() const {
        return m_settings.backgroundImageEnabled ? m_settings.backgroundImagePath : QString();
    }

    // 读取并二次规范化，不逐项发送 changed 信号；启动消费者自行读取快照。
    void load();

public slots:
    // 以下 setter 在规范化值变化后发送对应信号并保存整份设置。
    // 刷新率非法值回退默认值。
    void setVisualizationRefreshRate(int refreshRate);
    // 确认图形选择，同时在传统模式下关闭增强特效。
    void setGraphicsMode(settings::GraphicsMode mode);
    // 更新简谱条显示开关。
    void setShowNotationStrip(bool show);
    // 主题、音符配色和标题栏分别按值对象/平台规则规范化。
    void setThemeMode(settings::ThemeMode mode);
    void setNoteColorMode(settings::NoteColorMode mode);
    void setTitleBarMode(settings::TitleBarMode mode);
    // 路径 setter 仅规范化绝对路径，不负责解码图片或验证音源加载成功。
    void setSoundFontPath(const QString& path);
    void setFfmpegPath(const QString& path);
    // 切换 PATH 检测，保留手动目录。
    void setFfmpegUsePath(bool enabled);
    void setBackgroundImagePath(const QString& path);
    // 背景开关独立于路径，透明度夹取到 0..100。
    void setBackgroundImageEnabled(bool enabled);
    void setBackgroundImageAlignment(settings::BackgroundImageAlignment alignment);
    void setBackgroundImageOpacity(int opacity);
    // 仅当前构建支持 Vulkan 且选择 Vulkan 模式时允许开启增强效果。
    void setVisualEffectsEnabled(bool enabled);
    // 非法特效等级回退中档。
    void setVisualEffectsLevel(settings::VisualEffectLevel level);

signals:
    // 字段信号携带最终内存值；消费者据此更新 UI 或渲染后端。
    void visualizationRefreshRateChanged(int refreshRate);
    void graphicsModeChanged(midi_play::settings::GraphicsMode mode);
    void showNotationStripChanged(bool show);
    void themeModeChanged(midi_play::settings::ThemeMode mode);
    void noteColorModeChanged(midi_play::settings::NoteColorMode mode);
    void titleBarModeChanged(midi_play::settings::TitleBarMode mode);
    void soundFontPathChanged(const QString& path);
    void ffmpegPathChanged(const QString& path);
    void ffmpegUsePathChanged(bool enabled);
    void backgroundImagePathChanged(const QString& path);
    void backgroundImageEnabledChanged(bool enabled);
    void backgroundImageAlignmentChanged(midi_play::settings::BackgroundImageAlignment alignment);
    void backgroundImageOpacityChanged(int opacity);
    void visualEffectsEnabledChanged(bool enabled);
    void visualEffectsLevelChanged(midi_play::settings::VisualEffectLevel level);
    // 读取警告与保存失败分开发送，不与各字段的正常变化混用。
    void settingsLoadWarning(const QString& message);
    void settingsSaveFailed(const QString& message);

private:
    // 去除空白后转绝对路径；SoundFont/背景同时清理路径分隔及冗余段。
    static QString normalizeSoundFontPath(const QString& path);
    static QString normalizeFfmpegPath(const QString& path);
    static QString normalizeBackgroundImagePath(const QString& path);
    // 保存失败通知但不回滚；成功保存后清除启动时的过期读取警告。
    void persistSettings();

    std::unique_ptr<ISettingsStore> m_store;
    settings::PlayerSettings m_settings;
    QString m_lastLoadWarning;
};

} // namespace midi_play::app
