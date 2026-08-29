#include "settingsservice.h"

#include "isettingsstore.h"

namespace midi_play::app {

SettingsService::SettingsService(std::unique_ptr<ISettingsStore> store, QObject* parent)
    : QObject(parent), m_store(std::move(store))
{
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

    if (!m_store) {
        return;
    }

    QString error;
    if (!m_store->save(m_settings, &error) && !error.isEmpty()) {
        emit settingsSaveFailed(error);
    }
}

} // namespace midi_play::app
