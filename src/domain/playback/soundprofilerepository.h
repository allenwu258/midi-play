#pragma once

#include "playbacktypes.h"

#include <QMap>

namespace midi_play::playback {

struct SoundProfile {
    QString name;
    QMap<QString, AudioResource> resources;

    AudioResource resourceFor(const PlaybackSetupData& setup) const
    {
        return resources.value(setup.soundId);
    }
};

class SoundProfileRepository final {
public:
    void registerResource(const PlaybackSetupData& setup, const AudioResource& resource);
    const SoundProfile& basicProfile() const { return m_basic; }

private:
    SoundProfile m_basic {QStringLiteral("Basic"), {}};
};

} // namespace midi_play::playback
