// 设置服务实现：按能力约束外观状态，在变更通知之后执行持久化。
#include "settingsservice.h"

#include "isettingsstore.h"

#include <QDir>
#include <QFileInfo>

#include <utility>
#include <algorithm>

namespace midi_play::app {

// 接管存储实现，并注册可跨 Qt 信号边界传递的设置枚举。
SettingsService::SettingsService(std::unique_ptr<ISettingsStore> store, QObject* parent)
    : QObject(parent), m_store(std::move(store))
{
    qRegisterMetaType<midi_play::settings::TitleBarMode>();
    qRegisterMetaType<midi_play::settings::GraphicsMode>();
    qRegisterMetaType<midi_play::settings::ThemeMode>();
    qRegisterMetaType<midi_play::settings::NoteColorMode>();
    qRegisterMetaType<midi_play::settings::VisualEffectLevel>();
    qRegisterMetaType<midi_play::settings::BackgroundImageAlignment>();
    qRegisterMetaType<midi_play::settings::BackgroundMode>();
}

// 启动时加载并规范化完整设置；只发布读取警告，界面从快照取得初始值。
void SettingsService::load()
{
    if (!m_store) {
        return;
    }

    QString warning;
    // 存储实现的校验不替代应用约束；完整规范化后一次替换快照，不逐字段发布变化。
    auto loadedSettings = m_store->load(&warning);
    loadedSettings.visualizationRefreshRate =
        settings::normalizeVisualizationRefreshRate(loadedSettings.visualizationRefreshRate);
    loadedSettings.titleBarMode = settings::normalizeTitleBarMode(loadedSettings.titleBarMode);
    loadedSettings.graphicsMode = settings::normalizeGraphicsMode(loadedSettings.graphicsMode);
    loadedSettings.themeMode = settings::normalizeThemeMode(loadedSettings.themeMode);
    loadedSettings.noteColorMode = settings::normalizeNoteColorMode(loadedSettings.noteColorMode);
    loadedSettings.visualEffectsLevel = settings::normalizeVisualEffectLevel(
        loadedSettings.visualEffectsLevel);
    if (loadedSettings.graphicsMode == settings::GraphicsMode::Traditional)
        loadedSettings.visualEffectsEnabled = false;
#if !MIDI_PLAY_HAS_VULKAN
    // 传统构建可读取 Vulkan 构建的配置，保留后端偏好，但不向当前渲染器开放增强效果。
    loadedSettings.visualEffectsEnabled = false;
#endif
    loadedSettings.soundFontPath = normalizeSoundFontPath(loadedSettings.soundFontPath);
    loadedSettings.ffmpegPath = normalizeFfmpegPath(loadedSettings.ffmpegPath);
    loadedSettings.backgroundImagePath = normalizeBackgroundImagePath(loadedSettings.backgroundImagePath);
    loadedSettings.backgroundVideoPath = normalizeBackgroundImagePath(loadedSettings.backgroundVideoPath);
    loadedSettings.backgroundMode = settings::normalizeBackgroundMode(loadedSettings.backgroundMode);
    if (loadedSettings.backgroundMode == settings::BackgroundMode::None && loadedSettings.backgroundImageEnabled)
        loadedSettings.backgroundMode = settings::BackgroundMode::Image;
    loadedSettings.backgroundImageEnabled = loadedSettings.backgroundMode == settings::BackgroundMode::Image;
    loadedSettings.backgroundImageAlignment = settings::normalizeBackgroundImageAlignment(
        loadedSettings.backgroundImageAlignment);
    loadedSettings.backgroundImageOpacity = std::clamp(
        loadedSettings.backgroundImageOpacity, 0, 100);
    loadedSettings.backgroundImageMaximumDimension = settings::normalizeBackgroundImageMaximumDimension(
        loadedSettings.backgroundImageMaximumDimension);
    m_settings = loadedSettings;
    m_lastLoadWarning = warning;
    if (!warning.isEmpty()) {
        emit settingsLoadWarning(warning);
    }
}

// 设置写入统一遵循：规范化、相等检查、更新内存、发布信号、保存快照。
void SettingsService::setVisualizationRefreshRate(int refreshRate)
{
    const int normalizedRefreshRate = settings::normalizeVisualizationRefreshRate(refreshRate);
    if (m_settings.visualizationRefreshRate == normalizedRefreshRate) {
        return;
    }

    m_settings.visualizationRefreshRate = normalizedRefreshRate;
    emit visualizationRefreshRateChanged(normalizedRefreshRate);

    persistSettings();
}

// 同时维护后端选择确认标记和特效能力约束，一次保存最终设置状态。
void SettingsService::setGraphicsMode(settings::GraphicsMode mode)
{
    const auto normalizedMode = settings::normalizeGraphicsMode(mode);
    const bool modeChanged = m_settings.graphicsMode != normalizedMode;
    // 首次确认即使与默认模式相同也需通知并保存，记录已确定的后端选择。
    const bool modeNeedsResolution = !m_settings.graphicsModeConfigured;
    const bool effectsChanged = normalizedMode == settings::GraphicsMode::Traditional
        && m_settings.visualEffectsEnabled;
    if (!modeChanged && !effectsChanged && !modeNeedsResolution) {
        return;
    }

    m_settings.graphicsMode = normalizedMode;
    m_settings.graphicsModeConfigured = true;
    if (modeChanged || modeNeedsResolution) emit graphicsModeChanged(normalizedMode);
    if (effectsChanged) {
        m_settings.visualEffectsEnabled = false;
        emit visualEffectsEnabledChanged(false);
    }
    persistSettings();
}

void SettingsService::setShowNotationStrip(bool show)
{
    if (m_settings.showNotationStrip == show) {
        return;
    }

    m_settings.showNotationStrip = show;
    emit showNotationStripChanged(show);
    persistSettings();
}

void SettingsService::setThemeMode(settings::ThemeMode mode)
{
    const auto normalized = settings::normalizeThemeMode(mode);
    if (m_settings.themeMode == normalized) {
        return;
    }
    m_settings.themeMode = normalized;
    emit themeModeChanged(normalized);
    persistSettings();
}

void SettingsService::setNoteColorMode(settings::NoteColorMode mode)
{
    const auto normalized = settings::normalizeNoteColorMode(mode);
    if (m_settings.noteColorMode == normalized) return;
    m_settings.noteColorMode = normalized;
    emit noteColorModeChanged(normalized);
    persistSettings();
}

void SettingsService::setTitleBarMode(settings::TitleBarMode mode)
{
    const auto normalizedMode = settings::normalizeTitleBarMode(mode);
    if (m_settings.titleBarMode == normalizedMode) {
        return;
    }

    m_settings.titleBarMode = normalizedMode;
    emit titleBarModeChanged(normalizedMode);

    persistSettings();
}

void SettingsService::setSoundFontPath(const QString& path)
{
    const QString normalizedPath = normalizeSoundFontPath(path);
    if (m_settings.soundFontPath == normalizedPath) {
        return;
    }

    m_settings.soundFontPath = normalizedPath;
    emit soundFontPathChanged(soundFontPath());
    persistSettings();
}

void SettingsService::setFfmpegPath(const QString& path)
{
    const QString normalizedPath = normalizeFfmpegPath(path);
    if (m_settings.ffmpegPath == normalizedPath) return;
    m_settings.ffmpegPath = normalizedPath;
    emit ffmpegPathChanged(m_settings.ffmpegPath);
    persistSettings();
}

void SettingsService::setFfmpegUsePath(bool enabled)
{
    if (m_settings.ffmpegUsePath == enabled) return;
    m_settings.ffmpegUsePath = enabled;
    emit ffmpegUsePathChanged(enabled);
    persistSettings();
}

void SettingsService::setBackgroundImagePath(const QString& path)
{
    const QString normalizedPath = normalizeBackgroundImagePath(path);
    if (m_settings.backgroundImagePath == normalizedPath) return;

    m_settings.backgroundImagePath = normalizedPath;
    emit backgroundImagePathChanged(backgroundImagePath());
    persistSettings();
}

void SettingsService::setBackgroundImageEnabled(bool enabled)
{
    setBackgroundMode(enabled ? settings::BackgroundMode::Image : settings::BackgroundMode::None);
}

void SettingsService::setBackgroundMode(settings::BackgroundMode mode)
{
    mode = settings::normalizeBackgroundMode(mode);
    if (m_settings.backgroundMode == mode) return;
    const bool imageEnabled = mode == settings::BackgroundMode::Image;
    const bool imageChanged = imageEnabled != m_settings.backgroundImageEnabled;
    m_settings.backgroundMode = mode;
    m_settings.backgroundImageEnabled = imageEnabled;
    if (imageChanged) emit backgroundImageEnabledChanged(imageEnabled);
    emit backgroundModeChanged(mode);
    persistSettings();
}

void SettingsService::setBackgroundVideoPath(const QString& path)
{
    const auto normalized = normalizeBackgroundImagePath(path);
    if (m_settings.backgroundVideoPath == normalized) return;
    m_settings.backgroundVideoPath = normalized;
    emit backgroundVideoPathChanged(normalized);
    persistSettings();
}

void SettingsService::setBackgroundVideoMuted(bool muted)
{
    if (m_settings.backgroundVideoMuted == muted) return;
    m_settings.backgroundVideoMuted = muted;
    emit backgroundVideoMutedChanged(muted);
    persistSettings();
}

void SettingsService::setBackgroundImageAlignment(settings::BackgroundImageAlignment alignment)
{
    const auto normalized = settings::normalizeBackgroundImageAlignment(alignment);
    if (m_settings.backgroundImageAlignment == normalized) return;
    m_settings.backgroundImageAlignment = normalized;
    emit backgroundImageAlignmentChanged(normalized);
    persistSettings();
}

void SettingsService::setBackgroundImageOpacity(int opacity)
{
    const int normalized = std::clamp(opacity, 0, 100);
    if (m_settings.backgroundImageOpacity == normalized) return;
    m_settings.backgroundImageOpacity = normalized;
    emit backgroundImageOpacityChanged(normalized);
    persistSettings();
}

void SettingsService::setBackgroundImageMaximumDimension(int dimension)
{
    const int normalized = settings::normalizeBackgroundImageMaximumDimension(dimension);
    if (m_settings.backgroundImageMaximumDimension == normalized) return;
    m_settings.backgroundImageMaximumDimension = normalized;
    emit backgroundImageMaximumDimensionChanged(normalized);
    persistSettings();
}

void SettingsService::setVisualEffectsEnabled(bool enabled)
{
    // 构建能力和当前后端共同限制开关；不能仅凭用户请求开启未实现的渲染效果。
#if !MIDI_PLAY_HAS_VULKAN
    enabled = false;
#else
    if (enabled && m_settings.graphicsMode == settings::GraphicsMode::Traditional)
        enabled = false;
#endif
    if (m_settings.visualEffectsEnabled == enabled) return;
    m_settings.visualEffectsEnabled = enabled;
    emit visualEffectsEnabledChanged(enabled);
    persistSettings();
}

void SettingsService::setVisualEffectsLevel(settings::VisualEffectLevel level)
{
    const auto normalized = settings::normalizeVisualEffectLevel(level);
    if (m_settings.visualEffectsLevel == normalized) return;
    m_settings.visualEffectsLevel = normalized;
    emit visualEffectsLevelChanged(normalized);
    persistSettings();
}

// 路径规范化只消除空白和相对路径；文件内容校验由资源加载服务负责。
QString SettingsService::normalizeSoundFontPath(const QString& path)
{
    const QString trimmedPath = path.trimmed();
    if (trimmedPath.isEmpty()) {
        return {};
    }

    return QDir::cleanPath(QFileInfo(trimmedPath).absoluteFilePath());
}

// 保存绝对路径但不检查存在性，允许检测服务独立报告当前可执行文件状态。
QString SettingsService::normalizeFfmpegPath(const QString& path)
{
    const QString trimmed = path.trimmed();
    if (trimmed.isEmpty()) return {};
    return QFileInfo(trimmed).absoluteFilePath();
}

// 保留空路径语义；非空路径规范化后仍须由图片加载器检查能否解码。
QString SettingsService::normalizeBackgroundImagePath(const QString& path)
{
    const QString trimmedPath = path.trimmed();
    if (trimmedPath.isEmpty()) return {};
    return QDir::cleanPath(QFileInfo(trimmedPath).absoluteFilePath());
}

// 保存失败只报告错误，不回滚已生效的内存设置和此前发布的变更信号。
void SettingsService::persistSettings()
{
    if (!m_store) {
        return;
    }

    QString error;
    if (!m_store->save(m_settings, &error)) {
        emit settingsSaveFailed(error.isEmpty() ? QStringLiteral("无法保存设置") : error);
        return;
    }
    // 修复后的配置已安全落盘，后续设置对话框不再把启动警告显示为当前错误。
    m_lastLoadWarning.clear();
}

} // namespace midi_play::app
