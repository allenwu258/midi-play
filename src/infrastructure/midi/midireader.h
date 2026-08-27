#pragma once

#include "domain/music/musicdocument.h"
#include "infrastructure/musicxml/musicxmlreader.h"

namespace midi_play::midi {

class MidiReader final {
public:
    musicxml::ReadResult read(const QString& path) const;
};

} // namespace midi_play::midi
