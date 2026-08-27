#include "soundprofilerepository.h"

namespace midi_play::playback {

void SoundProfileRepository::registerResource(const PlaybackSetupData& setup, const AudioResource& resource)
{
    m_basic.resources.insert(setup.soundId, resource);
}

void SoundProfileRepository::registerResource(const PlaybackSetupData& setup,
                                               const AudioResourceMeta& metadata,
                                               const QVector<SoundPreset>& presets)
{
    m_basic.metadata.insert(setup.soundId, metadata);
    m_basic.presets.insert(setup.soundId, presets);
    m_basic.resources.insert(setup.soundId, AudioResource {metadata.path, -1});
}

QVector<AudioResourceMeta> SoundProfileRepository::availableResources() const
{
    QVector<AudioResourceMeta> result;
    result.reserve(m_basic.metadata.size());
    for (auto it = m_basic.metadata.cbegin(); it != m_basic.metadata.cend(); ++it) {
        result.push_back(it.value());
    }
    return result;
}

QVector<SoundPreset> SoundProfileRepository::availableSoundPresets(const PlaybackSetupData& setup) const
{
    return m_basic.soundPresets(setup);
}

} // namespace midi_play::playback
