#include "midireader.h"

#include "mididocumentbuilder.h"
#include "midifileparser.h"
#include "midinormalizer.h"

namespace midi_play::midi {

music::ReadResult MidiReader::read(const QString& path) const
{
    const auto parsed = MidiFileParser().parse(path);
    if (!parsed.ok()) return {nullptr, parsed.error};
    const auto normalized = MidiNormalizer().normalize(*parsed.file);
    if (!normalized.ok()) return {nullptr, normalized.error};
    return MidiDocumentBuilder().build(*normalized.file);
}

} // namespace midi_play::midi
