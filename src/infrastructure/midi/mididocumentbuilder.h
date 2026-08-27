#pragma once

#include "miditypes.h"
#include "domain/music/musicreadresult.h"

namespace midi_play::midi {

class MidiDocumentBuilder final {
public:
    music::ReadResult build(const MidiNormalizedFile& source) const;
};

} // namespace midi_play::midi
