#include "settingsservice.h"

#include "isettingsstore.h"

#include <QDir>
#include <QFileInfo>

#include <utility>
#include <algorithm>

namespace midi_play::app {

SettingsService::SettingsService(std::unique_ptr<ISettingsStore> store, QObject* parent)
    : QObject(parent), m_store(std::move(store))
{
    qRegisterMetaType<midi_play::settings::TitleBarMode>();
    qRegisterMetaType<midi_play::settings::GraphicsMode>();
    qRegisterMetaType<midi_play::settings::ThemeMode>();
    qRegisterMetaType<midi_play::settings::NoteColorMode>();
    qRegisterMetaType<midi_play::settings::VisualEffectLevel>();
    qRegisterMetaType<midi_play::settings::BackgroundImageAlignment>();
}

void SettingsService::load()
{
    if (!m_store) {
        return;
    }

    QString warning;
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
    // A traditional-only build may still read a settings file created by a
    // Vulkan build. Keep the preference for a future Vulkan build, but never
    // expose an enabled effect state to the current renderer.
    loadedSettings.visualEffectsEnabled = false;
#endif
    loadedSettings.soundFontPath = normalizeSoundFontPath(loadedSettings.soundFontPath);
    loadedSettings.backgroundImagePath = normalizeBackgroundImagePath(loadedSettings.backgroundImagePath);
    loadedSettings.backgroundImageAlignment = settings::normalizeBackgroundImageAlignment(
        loadedSettings.backgroundImageAlignment);
    loadedSettings.backgroundImageOpacity = std::clamp(
        loadedSettings.backgroundImageOpacity, 0, 100);
    m_settings = loadedSettings;
    m_lastLoadWarning = warning;
    if (!warning.isEmpty()) {
        emit settingsLoadWarning(warning);
    }
}

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

void SettingsService::setGraphicsMode(settings::GraphicsMode mode)
{
    const auto normalizedMode = settings::normalizeGraphicsMode(mode);
    const bool modeChanged = m_settings.graphicsMode != normalizedMode;
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
    if (m_settings.backgroundImageEnabled == enabled) return;
    m_settings.backgroundImageEnabled = enabled;
    emit backgroundImageEnabledChanged(enabled);
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

void SettingsService::setVisualEffectsEnabled(bool enabled)
{
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

QString SettingsService::normalizeSoundFontPath(const QString& path)
{
    const QString trimmedPath = path.trimmed();
    if (trimmedPath.isEmpty()) {
        return {};
    }

    return QDir::cleanPath(QFileInfo(trimmedPath).absoluteFilePath());
}

QString SettingsService::normalizeBackgroundImagePath(const QString& path)
{
    const QString trimmedPath = path.trimmed();
    if (trimmedPath.isEmpty()) return {};
    return QDir::cleanPath(QFileInfo(trimmedPath).absoluteFilePath());
}

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
    // Once a repaired configuration is safely stored, a future settings
    // dialog must not present the load-time warning as a current error.
    m_lastLoadWarning.clear();
}

} // namespace midi_play::app
