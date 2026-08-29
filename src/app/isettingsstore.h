#pragma once

#include "domain/settings/playersettings.h"

#include <QString>

namespace midi_play::app {

class ISettingsStore {
public:
    virtual ~ISettingsStore() = default;

    virtual settings::PlayerSettings load(QString* warning) = 0;
    virtual bool save(const settings::PlayerSettings& settings, QString* error) = 0;
};

} // namespace midi_play::app
