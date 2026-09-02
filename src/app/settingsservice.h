#pragma once

#include "domain/settings/playersettings.h"
#include "isettingsstore.h"

#include <QObject>

#include <memory>

namespace midi_play::app {

class SettingsService final : public QObject {
    Q_OBJECT
public:
    explicit SettingsService(std::unique_ptr<ISettingsStore> store, QObject* parent = nullptr);

    const settings::PlayerSettings& settings() const noexcept { return m_settings; }
    int visualizationRefreshRate() const noexcept { return m_settings.visualizationRefreshRate; }
    settings::TitleBarMode titleBarMode() const noexcept { return m_settings.titleBarMode; }

    void load();

public slots:
    void setVisualizationRefreshRate(int refreshRate);
    void setTitleBarMode(settings::TitleBarMode mode);

signals:
    void visualizationRefreshRateChanged(int refreshRate);
    void titleBarModeChanged(midi_play::settings::TitleBarMode mode);
    void settingsLoadWarning(const QString& message);
    void settingsSaveFailed(const QString& message);

private:
    std::unique_ptr<ISettingsStore> m_store;
    settings::PlayerSettings m_settings;
};

} // namespace midi_play::app
