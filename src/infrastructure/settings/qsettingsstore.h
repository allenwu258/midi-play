#pragma once

#include "app/isettingsstore.h"

#include <QString>

namespace midi_play::infrastructure::settings {

class QSettingsStore final : public app::ISettingsStore {
public:
    explicit QSettingsStore(QString settingsPath = {});

    midi_play::settings::PlayerSettings load(QString* warning) override;
    bool save(const midi_play::settings::PlayerSettings& settings, QString* error) override;

    const QString& settingsPath() const noexcept { return m_settingsPath; }

    static QString defaultSettingsPath();

private:
    QString m_settingsPath;
};

} // namespace midi_play::infrastructure::settings
