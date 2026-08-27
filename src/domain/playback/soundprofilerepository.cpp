#include "soundprofilerepository.h"

namespace midi_play::playback {

void SoundProfileRepository::registerResource(const PlaybackSetupData& setup, const AudioResource& resource)
{
    m_basic.resources.insert(setup.soundId, resource);
}

} // namespace midi_play::playback
