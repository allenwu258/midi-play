#pragma once

#include "domain/music/musicdocument.h"

#include <QByteArray>
#include <QVector>
#include <QString>
#include <QHash>

#include <cstdint>
#include <memory>

namespace midi_play::midi {

struct MidiFileHeader {
    int format = 1;
    int trackCount = 0;
    int division = 480;
    bool smpte = false;
    int smpteFramesPerSecond = 0;
    int ticksPerFrame = 0;
};

enum class MidiMessageKind {
    NoteOn,
    NoteOff,
    PolyPressure,
    ControlChange,
    ProgramChange,
    ChannelPressure,
    PitchBend,
    Meta,
    SysEx
};

struct MidiRawEvent {
    int sourceTrack = 0;
    int port = 0;
    int channel = 0;
    midi_play::music::Tick tick = 0;
    quint64 sequence = 0;
    MidiMessageKind kind = MidiMessageKind::Meta;
    int data1 = 0;
    int data2 = 0;
    int metaType = -1;
    QByteArray payload;
};

struct MidiRawTrack {
    int index = 0;
    QString name;
    QVector<MidiRawEvent> events;
};

struct MidiParsedFile {
    MidiFileHeader header;
    QVector<MidiRawTrack> tracks;
    QString title;
};

struct MidiNoteSpan {
    int sourceTrack = 0;
    int port = 0;
    int channel = 0;
    int pitch = 60;
    int velocity = 90;
    int program = 0;
    music::Tick start = 0;
    music::Tick keyRelease = 0;
    music::Tick audibleEnd = 0;
    quint64 sequence = 0;
    bool implicitOff = false;
};

struct MidiLogicalTrack {
    QString id;
    QString name;
    int sourceTrack = 0;
    int port = 0;
    int channel = 0;
    int program = 0;
    int bankMsb = 0;
    int bankLsb = 0;
    bool percussion = false;
    QVector<MidiNoteSpan> notes;
    QVector<MidiRawEvent> events;
};

struct MidiReadDiagnostics {
    int orphanNoteOffs = 0;
    int implicitNoteOffs = 0;
    int unknownMessages = 0;
};

struct MidiNormalizedFile {
    MidiFileHeader header;
    QVector<MidiLogicalTrack> tracks;
    QVector<MidiRawEvent> globalEvents;
    QVector<music::TempoChange> tempos;
    QString title;
    music::Tick duration = 0;
    MidiReadDiagnostics diagnostics;
};

} // namespace midi_play::midi
