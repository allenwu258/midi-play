#pragma once

#include "miditypes.h"

namespace midi_play::midi {

struct MidiParseResult {
    std::shared_ptr<MidiParsedFile> file;
    QString error;
    bool ok() const { return file && error.isEmpty(); }
};

class MidiFileParser final {
public:
    MidiParseResult parse(const QString& path) const;
};

} // namespace midi_play::midi
