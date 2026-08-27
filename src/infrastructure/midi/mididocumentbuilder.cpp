#include "mididocumentbuilder.h"

#include "midinormalizer.h"

#include <algorithm>

namespace midi_play::midi {
namespace {

void appendMeta(const MidiNormalizedFile& source, music::MusicDocument& document)
{
    if (document.tracks().isEmpty()) return;
    for (const auto& event : source.globalEvents) {
        if (event.kind != MidiMessageKind::Meta) continue;
        if (event.metaType == 0x58 && event.payload.size() >= 2) {
            const int beats = static_cast<unsigned char>(event.payload.at(0));
            const int denominatorPower = static_cast<unsigned char>(event.payload.at(1));
            document.tracks().front().timeSignatures.push_back({event.tick, beats,
                1 << std::clamp(denominatorPower, 0, 6)});
        } else if (event.metaType == 0x59 && event.payload.size() >= 2) {
            const int fifths = static_cast<qint8>(event.payload.at(0));
            const int mode = static_cast<unsigned char>(event.payload.at(1));
            document.tracks().front().keySignatures.push_back({event.tick, fifths,
                mode == 0 ? QStringLiteral("major") : QStringLiteral("minor")});
        }
    }
}

} // namespace

music::ReadResult MidiDocumentBuilder::build(const MidiNormalizedFile& source) const
{
    auto document = std::make_shared<music::MusicDocument>();
    document->setTitle(source.title);
    document->tempos() = source.tempos;
    quint64 nextNoteId = 1;

    for (const auto& normalizedTrack : source.tracks) {
        music::Track track;
        track.id = normalizedTrack.id;
        track.name = normalizedTrack.name;
        track.channel = normalizedTrack.channel;
        track.program = normalizedTrack.program;
        track.percussion = normalizedTrack.percussion;

        int bankMsb = 0;
        int bankLsb = 0;
        for (const auto& event : normalizedTrack.events) {
            switch (event.kind) {
            case MidiMessageKind::ControlChange:
                track.controlChanges.push_back({event.tick, event.channel, event.data1, event.data2, event.sequence});
                if (event.data1 == 0) bankMsb = std::clamp(event.data2, 0, 127);
                if (event.data1 == 32) bankLsb = std::clamp(event.data2, 0, 127);
                break;
            case MidiMessageKind::ProgramChange:
                track.instrumentChanges.push_back({event.tick, event.channel,
                                                    std::clamp(event.data1, 0, 127),
                                                    normalizedTrack.id, bankMsb, bankLsb, event.sequence});
                break;
            case MidiMessageKind::PitchBend:
                track.pitchBendChanges.push_back({event.tick, event.channel,
                                                  std::clamp(event.data2, 0, 16383), event.sequence});
                break;
            case MidiMessageKind::ChannelPressure:
                track.channelPressureChanges.push_back({event.tick, event.channel,
                                                         std::clamp(event.data1, 0, 127), event.sequence});
                break;
            case MidiMessageKind::PolyPressure:
                track.polyPressureChanges.push_back({event.tick, event.channel,
                                                      std::clamp(event.data1, 0, 127),
                                                      std::clamp(event.data2, 0, 127), event.sequence});
                break;
            default:
                break;
            }
        }

        for (const auto& note : normalizedTrack.notes) {
            const music::Tick end = std::max(note.start + 1, note.audibleEnd);
            music::NoteEvent event;
            event.start = note.start;
            event.duration = std::max<music::Tick>(1, end - note.start);
            event.pitch = note.pitch;
            event.velocity = note.velocity;
            event.channel = note.channel;
            event.program = note.program;
            event.sequence = note.sequence;
            event.noteId = nextNoteId++;
            track.notes.push_back(event);
        }
        std::sort(track.notes.begin(), track.notes.end(), [](const auto& left, const auto& right) {
            if (left.start != right.start) return left.start < right.start;
            if (left.pitch != right.pitch) return left.pitch < right.pitch;
            if (left.velocity != right.velocity) return left.velocity < right.velocity;
            return left.sequence < right.sequence;
        });
        if (!track.notes.isEmpty()) document->tracks().push_back(std::move(track));
    }

    appendMeta(source, *document);
    document->setDuration(std::max<music::Tick>(1, source.duration));
    document->rebuildMeasureGrid();
    document->rebuildTempoMap();
    if (!document->isValid()) return {nullptr, QStringLiteral("MIDI 未包含可播放音符")};
    return {std::move(document), {}};
}

} // namespace midi_play::midi
