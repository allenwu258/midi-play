#pragma once

#include "domain/settings/playersettings.h"
#include "isettingsstore.h"

#include <QObject>
#include <QString>

#include <memory>

namespace midi_play::app {

class SettingsService final : public QObject {
    Q_OBJECT
public:
    explicit SettingsService(std::unique_ptr<ISettingsStore> store, QObject* parent = nullptr);
    SettingsService(std::unique_ptr<ISettingsStore> store,
                    QString defaultSoundFontPath,
                    QObject* parent = nullptr);

    const settings::PlayerSettings& settings() const noexcept { return m_settings; }
    int visualizationRefreshRate() const noexcept { return m_settings.visualizationRefreshRate; }
    settings::TitleBarMode titleBarMode() const noexcept { return m_settings.titleBarMode; }
    const QString& defaultSoundFontPath() const noexcept { return m_defaultSoundFontPath; }
    QString soundFontPath() const;
    bool usesDefaultSoundFont() const noexcept { return m_settings.soundFontPathOverride.isEmpty(); }

    void load();

public slots:
    void setVisualizationRefreshRate(int refreshRate);
    void setTitleBarMode(settings::TitleBarMode mode);
    void setSoundFontPath(const QString& path);
    void resetSoundFontPath();

signals:
    void visualizationRefreshRateChanged(int refreshRate);
    void titleBarModeChanged(midi_play::settings::TitleBarMode mode);
    void soundFontPathChanged(const QString& path, bool usesDefault);
    void settingsLoadWarning(const QString& message);
    void settingsSaveFailed(const QString& message);

private:
    QString normalizeSoundFontPathOverride(const QString& path) const;
    void persistSettings();

    std::unique_ptr<ISettingsStore> m_store;
    settings::PlayerSettings m_settings;
    QString m_defaultSoundFontPath;
};

} // namespace midi_play::app
