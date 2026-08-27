#pragma once

#include "miditypes.h"

namespace midi_play::midi {

struct MidiNormalizeResult {
    std::shared_ptr<MidiNormalizedFile> file;
    MidiReadDiagnostics diagnostics;
    QString error;
    bool ok() const { return file && error.isEmpty(); }
};

class MidiNormalizer final {
public:
    MidiNormalizeResult normalize(const MidiParsedFile& source) const;
    static music::Tick scaleTick(music::Tick sourceTick, const MidiFileHeader& header);
};

} // namespace midi_play::midi
