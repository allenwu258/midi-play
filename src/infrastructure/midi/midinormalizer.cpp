#include "midinormalizer.h"

#include <QSet>
#include <QMap>

#include <algorithm>
#include <cmath>

namespace midi_play::midi {
namespace {

struct PendingNote {
    music::Tick start = 0;
    int velocity = 90;
    int program = 0;
    quint64 sequence = 0;
};

int controllerValue(const MidiRawEvent& event)
{
    return std::clamp(event.data2, 0, 127);
}

void appendTempo(const MidiRawEvent& event, const MidiFileHeader& header, music::Tick sequenceOffset,
                 QVector<music::TempoChange>& tempos)
{
    // SMPTE-timed files use a fixed tick rate; tempo meta-events do not alter
    // that rate and therefore must not override the synthetic 60 BPM map.
    if (header.smpte || event.kind != MidiMessageKind::Meta || event.metaType != 0x51
        || event.payload.size() != 3) return;
    const auto* bytes = reinterpret_cast<const unsigned char*>(event.payload.constData());
    const int microsecondsPerQuarter = (int(bytes[0]) << 16) | (int(bytes[1]) << 8) | int(bytes[2]);
    if (microsecondsPerQuarter <= 0) return;
    tempos.push_back({MidiNormalizer::scaleTick(event.tick + sequenceOffset, header),
                      60'000'000.0 / static_cast<double>(microsecondsPerQuarter)});
}

} // namespace

music::Tick MidiNormalizer::scaleTick(music::Tick sourceTick, const MidiFileHeader& header)
{
    if (header.division <= 0) return 0;
    return static_cast<music::Tick>(std::llround(static_cast<long double>(sourceTick)
                                                 * music::MusicDocument::kPpq / header.division));
}

MidiNormalizeResult MidiNormalizer::normalize(const MidiParsedFile& source) const
{
    auto result = std::make_shared<MidiNormalizedFile>();
    result->header = source.header;
    result->title = source.title;
    QVector<music::TempoChange> tempos;
    quint64 globalSequence = 0;

    music::Tick sequenceOffset = 0;
    for (const auto& rawTrack : source.tracks) {
        music::Tick rawTrackEnd = 0;
        for (const auto& rawEvent : rawTrack.events) rawTrackEnd = std::max(rawTrackEnd, rawEvent.tick);
        QMap<QPair<int, int>, QVector<const MidiRawEvent*>> byPortChannel;
        for (const auto& rawEvent : rawTrack.events) {
            MidiRawEvent normalized = rawEvent;
            normalized.tick = scaleTick(rawEvent.tick + sequenceOffset, source.header);
            normalized.sequence = globalSequence++;
            result->globalEvents.push_back(normalized);
            appendTempo(rawEvent, source.header, sequenceOffset, tempos);
            if (rawEvent.kind != MidiMessageKind::Meta && rawEvent.kind != MidiMessageKind::SysEx) {
                byPortChannel[qMakePair(rawEvent.port, rawEvent.channel)].push_back(&rawEvent);
            }
        }

        for (auto groupIt = byPortChannel.cbegin(); groupIt != byPortChannel.cend(); ++groupIt) {
            const int port = groupIt.key().first;
            const int channel = groupIt.key().second;
            const auto& channelEvents = groupIt.value();
            MidiLogicalTrack track;
            track.sourceTrack = rawTrack.index;
            track.port = port;
            track.channel = channel;
            track.id = QStringLiteral("midi-track-%1-port-%2-channel-%3")
                           .arg(rawTrack.index + 1).arg(track.port).arg(channel + 1);
            track.name = rawTrack.name.isEmpty() ? track.id : rawTrack.name;
            track.percussion = channel == 9;

            QHash<int, QVector<PendingNote>> pending;
            QVector<int> sustained;
            QSet<int> sostenutoCaptured;
            bool sustainDown = false;
            bool sostenutoDown = false;
            int currentProgram = 0;
            int bankMsb = 0;
            int bankLsb = 0;
            music::Tick lastTick = 0;

            auto appendClosedNote = [&](int pitch, const PendingNote& pendingNote,
                                        music::Tick keyRelease, music::Tick audibleEnd) {
                MidiNoteSpan span;
                span.sourceTrack = rawTrack.index;
                span.port = track.port;
                span.channel = channel;
                span.pitch = std::clamp(pitch, 0, 127);
                span.velocity = std::clamp(pendingNote.velocity, 1, 127);
                span.program = pendingNote.program;
                span.start = pendingNote.start;
                span.keyRelease = std::max(pendingNote.start + 1, keyRelease);
                span.audibleEnd = std::max(span.keyRelease, audibleEnd);
                span.sequence = pendingNote.sequence;
                track.notes.push_back(span);
                return track.notes.size() - 1;
            };

            auto closePendingNotes = [&](music::Tick tick, bool forceAudibleEnd) {
                for (auto pendingIt = pending.begin(); pendingIt != pending.end(); ++pendingIt) {
                    for (const PendingNote& pendingNote : pendingIt.value()) {
                        const int noteIndex = appendClosedNote(pendingIt.key(), pendingNote, tick, tick);
                        if (forceAudibleEnd) continue;
                        const bool heldByPedal = sustainDown
                            || (sostenutoDown && sostenutoCaptured.contains(pendingIt.key()));
                        if (heldByPedal) sustained.push_back(noteIndex);
                    }
                }
                pending.clear();
            };

            auto releaseHeld = [&](music::Tick tick) {
                for (const int index : sustained) {
                    if (index < 0 || index >= track.notes.size()) continue;
                    auto& note = track.notes[index];
                    if (!sustainDown && !sostenutoDown) note.audibleEnd = tick;
                }
                sustained.erase(std::remove_if(sustained.begin(), sustained.end(), [&](int index) {
                    return index < 0 || index >= track.notes.size()
                        || (!sustainDown && !sostenutoDown);
                }), sustained.end());
            };

            for (const MidiRawEvent* rawEvent : channelEvents) {
                const MidiRawEvent& event = *rawEvent;
                const music::Tick tick = scaleTick(event.tick + sequenceOffset, source.header);
                lastTick = std::max(lastTick, tick);
                MidiRawEvent normalized = event;
                normalized.tick = tick;
                normalized.sequence = event.sequence;
                track.events.push_back(normalized);

                switch (event.kind) {
                case MidiMessageKind::NoteOn:
                    pending[event.data1].push_back({tick, event.data2, currentProgram, event.sequence});
                    break;
                case MidiMessageKind::NoteOff: {
                    auto pendingIt = pending.find(event.data1);
                    if (pendingIt == pending.end() || pendingIt->isEmpty()) {
                        ++result->diagnostics.orphanNoteOffs;
                        continue;
                    }
                    const PendingNote note = pendingIt->takeLast();
                    if (pendingIt->isEmpty()) pending.erase(pendingIt);
                    const int noteIndex = appendClosedNote(event.data1, note, tick, tick);
                    const bool heldByPedal = sustainDown || (sostenutoDown && sostenutoCaptured.contains(event.data1));
                    if (heldByPedal) sustained.push_back(noteIndex);
                    break;
                }
                case MidiMessageKind::ProgramChange:
                    currentProgram = std::clamp(event.data1, 0, 127);
                    track.program = currentProgram;
                    track.bankMsb = bankMsb;
                    track.bankLsb = bankLsb;
                    break;
                case MidiMessageKind::ControlChange:
                    if (event.data1 == 0) bankMsb = controllerValue(event);
                    if (event.data1 == 32) bankLsb = controllerValue(event);
                    if (event.data1 == 64) {
                        const bool wasDown = sustainDown;
                        sustainDown = controllerValue(event) >= 64;
                        if (wasDown && !sustainDown) releaseHeld(tick);
                    } else if (event.data1 == 66) {
                        const bool wasDown = sostenutoDown;
                        sostenutoDown = controllerValue(event) >= 64;
                        if (!wasDown && sostenutoDown) {
                            for (auto pendingIt = pending.cbegin(); pendingIt != pending.cend(); ++pendingIt) {
                                if (!pendingIt->isEmpty()) sostenutoCaptured.insert(pendingIt.key());
                            }
                        } else if (wasDown && !sostenutoDown) {
                            releaseHeld(tick);
                            sostenutoCaptured.clear();
                        }
                    } else if (event.data1 == 123) {
                        closePendingNotes(tick, false);
                        releaseHeld(tick);
                    } else if (event.data1 == 120) {
                        closePendingNotes(tick, true);
                        for (const int index : sustained) {
                            if (index >= 0 && index < track.notes.size()) track.notes[index].audibleEnd = tick;
                        }
                        sustained.clear();
                    }
                    break;
                case MidiMessageKind::PolyPressure:
                case MidiMessageKind::ChannelPressure:
                case MidiMessageKind::PitchBend:
                case MidiMessageKind::Meta:
                case MidiMessageKind::SysEx:
                    break;
                }
            }

            for (auto pendingIt = pending.cbegin(); pendingIt != pending.cend(); ++pendingIt) {
                for (const PendingNote& note : pendingIt.value()) {
                    const int noteIndex = appendClosedNote(pendingIt.key(), note, lastTick, lastTick);
                    track.notes[noteIndex].implicitOff = true;
                    ++result->diagnostics.implicitNoteOffs;
                }
            }
            for (const int index : sustained) {
                if (index >= 0 && index < track.notes.size()) {
                    track.notes[index].audibleEnd = std::max(track.notes[index].keyRelease, lastTick);
                }
            }
            sustained.clear();

            std::sort(track.notes.begin(), track.notes.end(), [](const auto& left, const auto& right) {
                if (left.start != right.start) return left.start < right.start;
                if (left.sequence != right.sequence) return left.sequence < right.sequence;
                return left.pitch < right.pitch;
            });
            for (const auto& note : track.notes) {
                result->duration = std::max(result->duration, note.audibleEnd);
            }
            if (!track.notes.isEmpty()) result->tracks.push_back(std::move(track));
        }
        if (source.header.format == 2) {
            sequenceOffset += rawTrackEnd;
        }
    }

    std::sort(result->globalEvents.begin(), result->globalEvents.end(), [](const auto& left, const auto& right) {
        if (left.tick != right.tick) return left.tick < right.tick;
        return left.sequence < right.sequence;
    });
    std::sort(tempos.begin(), tempos.end(), [](const auto& left, const auto& right) { return left.tick < right.tick; });
    QVector<music::TempoChange> dedupedTempos;
    for (const auto& tempo : tempos) {
        if (!dedupedTempos.isEmpty() && dedupedTempos.back().tick == tempo.tick) dedupedTempos.back() = tempo;
        else dedupedTempos.push_back(tempo);
    }
    result->tempos = std::move(dedupedTempos);
    if (result->tempos.isEmpty()) {
        const double bpm = source.header.smpte ? 60.0 : 120.0;
        result->tempos.push_back({0, bpm});
    }
    const MidiReadDiagnostics diagnostics = result->diagnostics;
    return {std::move(result), diagnostics, {}};
}

} // namespace midi_play::midi
