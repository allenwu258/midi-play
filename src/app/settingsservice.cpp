#include "settingsservice.h"

#include "isettingsstore.h"

#include <QDir>
#include <QFileInfo>

#include <utility>

namespace midi_play::app {

SettingsService::SettingsService(std::unique_ptr<ISettingsStore> store, QObject* parent)
    : SettingsService(std::move(store), {}, parent)
{
}

SettingsService::SettingsService(std::unique_ptr<ISettingsStore> store,
                                 QString defaultSoundFontPath,
                                 QObject* parent)
    : QObject(parent), m_store(std::move(store))
    , m_defaultSoundFontPath(defaultSoundFontPath.trimmed().isEmpty()
          ? QString()
          : QDir::cleanPath(QFileInfo(defaultSoundFontPath).absoluteFilePath()))
{
    qRegisterMetaType<midi_play::settings::TitleBarMode>();
}

QString SettingsService::soundFontPath() const
{
    return usesDefaultSoundFont() ? m_defaultSoundFontPath : m_settings.soundFontPathOverride;
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
    loadedSettings.soundFontPathOverride =
        normalizeSoundFontPathOverride(loadedSettings.soundFontPathOverride);
    m_settings = loadedSettings;
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
    const QString normalizedOverride = normalizeSoundFontPathOverride(path);
    if (m_settings.soundFontPathOverride == normalizedOverride) {
        return;
    }

    m_settings.soundFontPathOverride = normalizedOverride;
    emit soundFontPathChanged(soundFontPath(), usesDefaultSoundFont());
    persistSettings();
}

void SettingsService::resetSoundFontPath()
{
    setSoundFontPath({});
}

QString SettingsService::normalizeSoundFontPathOverride(const QString& path) const
{
    const QString trimmedPath = path.trimmed();
    if (trimmedPath.isEmpty()) {
        return {};
    }

    const QFileInfo pathInfo(trimmedPath);
    const QString normalizedPath = QDir::cleanPath(pathInfo.absoluteFilePath());
    if (m_defaultSoundFontPath.isEmpty()) {
        return normalizedPath;
    }

    const QFileInfo defaultInfo(m_defaultSoundFontPath);
    const QString canonicalPath = pathInfo.canonicalFilePath();
    const QString canonicalDefaultPath = defaultInfo.canonicalFilePath();
    const QString comparablePath = canonicalPath.isEmpty() ? normalizedPath : canonicalPath;
    const QString comparableDefaultPath = canonicalDefaultPath.isEmpty()
        ? QDir::cleanPath(defaultInfo.absoluteFilePath()) : canonicalDefaultPath;
#if defined(Q_OS_WIN)
    if (comparablePath.compare(comparableDefaultPath, Qt::CaseInsensitive) == 0) {
#else
    if (comparablePath == comparableDefaultPath) {
#endif
        return {};
    }
    return normalizedPath;
}

void SettingsService::persistSettings()
{
    if (!m_store) {
        return;
    }

    QString error;
    if (!m_store->save(m_settings, &error) && !error.isEmpty()) {
        emit settingsSaveFailed(error);
    }
}

} // namespace midi_play::app
