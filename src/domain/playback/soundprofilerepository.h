#pragma once

#include "playbacktypes.h"

#include <QMap>

namespace midi_play::playback {

struct SoundProfile {
    QString name;
    QMap<QString, AudioResource> resources;
    QMap<QString, AudioResourceMeta> metadata;
    QMap<QString, QVector<SoundPreset>> presets;

    AudioResource resourceFor(const PlaybackSetupData& setup) const
    {
        return resources.value(setup.soundId);
    }

    AudioResourceMeta resourceMeta(const PlaybackSetupData& setup) const
    {
        return metadata.value(setup.soundId);
    }

    QVector<SoundPreset> soundPresets(const PlaybackSetupData& setup) const
    {
        return presets.value(setup.soundId);
    }
};

class SoundProfileRepository final {
public:
    void registerResource(const PlaybackSetupData& setup, const AudioResource& resource);
    void registerResource(const PlaybackSetupData& setup, const AudioResourceMeta& metadata,
                          const QVector<SoundPreset>& presets = {});
    QVector<AudioResourceMeta> availableResources() const;
    QVector<SoundPreset> availableSoundPresets(const PlaybackSetupData& setup) const;
    const SoundProfile& basicProfile() const { return m_basic; }

private:
    SoundProfile m_basic {QStringLiteral("Basic"), {}};
};

} // namespace midi_play::playback
