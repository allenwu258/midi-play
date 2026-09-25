#include "settingsservice.h"

#include "isettingsstore.h"

#include <QDir>
#include <QFileInfo>

#include <utility>

namespace midi_play::app {

SettingsService::SettingsService(std::unique_ptr<ISettingsStore> store, QObject* parent)
    : QObject(parent), m_store(std::move(store))
{
    qRegisterMetaType<midi_play::settings::TitleBarMode>();
    qRegisterMetaType<midi_play::settings::GraphicsMode>();
    qRegisterMetaType<midi_play::settings::ThemeMode>();
    qRegisterMetaType<midi_play::settings::NoteColorMode>();
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
    loadedSettings.soundFontPath = normalizeSoundFontPath(loadedSettings.soundFontPath);
    loadedSettings.backgroundImagePath = normalizeBackgroundImagePath(loadedSettings.backgroundImagePath);
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
    if (!modeChanged && m_settings.graphicsModeConfigured) {
        return;
    }

    m_settings.graphicsMode = normalizedMode;
    m_settings.graphicsModeConfigured = true;
    emit graphicsModeChanged(normalizedMode);
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
