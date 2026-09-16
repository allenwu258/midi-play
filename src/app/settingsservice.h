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
    settings::GraphicsMode graphicsMode() const noexcept { return m_settings.graphicsMode; }
    bool showNotationStrip() const noexcept { return m_settings.showNotationStrip; }
    settings::ThemeMode themeMode() const noexcept { return m_settings.themeMode; }
    settings::NoteColorMode noteColorMode() const noexcept { return m_settings.noteColorMode; }
    const QString& lastLoadWarning() const noexcept { return m_lastLoadWarning; }
    settings::TitleBarMode titleBarMode() const noexcept { return m_settings.titleBarMode; }
    const QString& defaultSoundFontPath() const noexcept { return m_defaultSoundFontPath; }
    QString soundFontPath() const;
    bool usesDefaultSoundFont() const noexcept { return m_settings.soundFontPathOverride.isEmpty(); }

    void load();

public slots:
    void setVisualizationRefreshRate(int refreshRate);
    void setGraphicsMode(settings::GraphicsMode mode);
    void setShowNotationStrip(bool show);
    void setThemeMode(settings::ThemeMode mode);
    void setNoteColorMode(settings::NoteColorMode mode);
    void setTitleBarMode(settings::TitleBarMode mode);
    void setSoundFontPath(const QString& path);
    void resetSoundFontPath();

signals:
    void visualizationRefreshRateChanged(int refreshRate);
    void graphicsModeChanged(midi_play::settings::GraphicsMode mode);
    void showNotationStripChanged(bool show);
    void themeModeChanged(midi_play::settings::ThemeMode mode);
    void noteColorModeChanged(midi_play::settings::NoteColorMode mode);
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
    QString m_lastLoadWarning;
};

} // namespace midi_play::app
