#pragma once

#include <QString>
#include <QVector>
#include <cstdint>

namespace midi_play::music {

using Tick = std::int64_t;

struct RawTime {
    Tick tick = 0;
    qint64 microseconds = 0;
};

struct GridTime {
    Tick tick = 0;
    int measureIndex = -1;
    double beat = 0.0;
};

struct WrittenPitch {
    int step = 0; // C=0 ... B=6
    int alter = 0;
    int octave = 4;
    int midiPitch = 60;
};

struct KeyContext {
    int fifths = 0;
    QString mode = QStringLiteral("major");

    int tonicPitchClass() const;
    int degreeFor(const WrittenPitch& pitch) const;
};

struct TempoChange {
    Tick tick = 0;
    double bpm = 120.0;
    quint64 sequence = 0;
};

struct NoteEvent {
    Tick start = 0;
    Tick duration = 0;
    int pitch = 60;
    int velocity = 90;
    int channel = 0;
    int program = 0;
    int voice = 1;
    int staff = 1;
    bool rest = false;
    bool grace = false;
    bool tieStart = false;
    bool tieStop = false;
    bool staccato = false;
    bool accent = false;
    bool tenuto = false;
    bool ghost = false;
    bool marcato = false;
    bool tremolo = false;
    quint64 sequence = 0;
    quint64 noteId = 0;
    RawTime rawStart;
    RawTime rawEnd;
    GridTime gridStart;
    GridTime gridEnd;
    WrittenPitch writtenPitch;
    int scaleDegree = 0;
    int accidental = 0;
    int measureIndex = -1;
    double beat = 0.0;
    quint64 chordId = 0;
    quint64 tieGroupId = 0;
    int lane = -1;
    bool hasWrittenPitch = false;
};

struct Measure {
    int number = 0;
    Tick start = 0;
    Tick duration = 0;
    bool repeatStart = false;
    bool repeatEnd = false;
    int repeatCount = 2;
    int endingNumber = 0;
    QString endingType;
    bool segno = false;
    bool coda = false;
    bool daCapo = false;
    bool dalSegno = false;
    bool toCoda = false;
    bool fine = false;
};

struct InstrumentChange {
    Tick tick = 0;
    int channel = 0;
    int program = 0;
    QString sourceId;
    int bankMsb = 0;
    int bankLsb = 0;
    quint64 sequence = 0;
};

struct ControlChange {
    Tick tick = 0;
    int channel = 0;
    int controller = 0;
    int value = 0;
    quint64 sequence = 0;
};

struct PitchBendChange {
    Tick tick = 0;
    int channel = 0;
    int value = 8192;
    quint64 sequence = 0;
};

struct ChannelPressureChange {
    Tick tick = 0;
    int channel = 0;
    int value = 0;
    quint64 sequence = 0;
};

struct PolyPressureChange {
    Tick tick = 0;
    int channel = 0;
    int pitch = 60;
    int value = 0;
    quint64 sequence = 0;
};

struct DynamicChange {
    Tick tick = 0;
    int velocity = 90;
};

struct TimeSignatureChange {
    Tick tick = 0;
    int beats = 4;
    int beatType = 4;
};

struct KeySignatureChange {
    Tick tick = 0;
    int fifths = 0;
    QString mode = QStringLiteral("major");
};

struct ClefChange {
    Tick tick = 0;
    int staff = 1;
    QString sign = QStringLiteral("G");
    int line = 2;
    int octaveChange = 0;
};

struct HairpinChange {
    Tick start = 0;
    Tick end = 0;
    bool crescendo = true;
};

struct ChordGroup {
    quint64 id = 0;
    Tick start = 0;
    Tick duration = 0;
    int voice = 1;
    int staff = 1;
    QVector<quint64> noteIds;
};

struct HoldNote {
    quint64 noteId = 0;
    Tick start = 0;
    Tick end = 0;
    int lane = -1;
};

struct TieGroup {
    quint64 id = 0;
    QVector<quint64> noteIds;
};

struct DrumMapEntry {
    int pitch = 0;
    QString name;
    int lane = 0;
    int staffLine = 0;
};

struct MarkerEvent {
    Tick tick = 0;
    QString text;
    quint64 sequence = 0;
};

struct LyricEvent {
    Tick tick = 0;
    QString text;
    int verse = 1;
    quint64 sequence = 0;
};

struct PlaybackSegment {
    Tick sourceStart = 0;
    Tick sourceEnd = 0;
    Tick outputStart = 0;
    int repeatPass = 0;
    int sourceMeasureIndex = -1;
};

struct RepeatJumpContext {
    int repeatStartIndex = 0;
    int repeatPass = 0;
    bool daCapoTaken = false;
    bool dalSegnoTaken = false;
    bool codaArmed = false;
};

class RepeatList final {
public:
    static RepeatList build(const QVector<Measure>& measures, Tick duration);

    const QVector<PlaybackSegment>& segments() const { return m_segments; }
    Tick duration() const { return m_duration; }

private:
    QVector<PlaybackSegment> m_segments;
    Tick m_duration = 0;
};

struct Track {
    QString id;
    QString name;
    int channel = 0;
    int program = 0;
    bool percussion = false;
    QVector<NoteEvent> notes;
    QVector<Measure> measures;
    QVector<InstrumentChange> instrumentChanges;
    QVector<ControlChange> controlChanges;
    QVector<PitchBendChange> pitchBendChanges;
    QVector<ChannelPressureChange> channelPressureChanges;
    QVector<PolyPressureChange> polyPressureChanges;
    QVector<DynamicChange> dynamics;
    QVector<TimeSignatureChange> timeSignatures;
    QVector<KeySignatureChange> keySignatures;
    QVector<ClefChange> clefs;
    QVector<HairpinChange> hairpins;
    QVector<ChordGroup> chords;
    QVector<HoldNote> holds;
    QVector<TieGroup> ties;
    QVector<DrumMapEntry> drumMap;
    QVector<MarkerEvent> markers;
    QVector<LyricEvent> lyrics;
};

class MusicDocument {
public:
    static constexpr int kPpq = 480;

    bool isValid() const { return !m_tracks.isEmpty() && m_duration > 0; }
    const QVector<Track>& tracks() const { return m_tracks; }
    QVector<Track>& tracks() { return m_tracks; }
    const QVector<Measure>& measures() const { return m_measures; }
    QVector<Measure>& measures() { return m_measures; }
    void rebuildMeasureGrid();
    const QVector<KeySignatureChange>& keySignatures() const { return m_keySignatures; }
    QVector<KeySignatureChange>& keySignatures() { return m_keySignatures; }
    const QVector<MarkerEvent>& markers() const { return m_markers; }
    QVector<MarkerEvent>& markers() { return m_markers; }
    const QVector<LyricEvent>& lyrics() const { return m_lyrics; }
    QVector<LyricEvent>& lyrics() { return m_lyrics; }
    const KeyContext& keyContext() const { return m_keyContext; }
    void setKeyContext(const KeyContext& value) { m_keyContext = value; }
    const QVector<TempoChange>& tempos() const { return m_tempos; }
    QVector<TempoChange>& tempos() { m_tempoMap.clear(); return m_tempos; }
    void rebuildTempoMap() const;
    Tick duration() const { return m_duration; }
    void setDuration(Tick value) { m_duration = value; }
    QString title() const { return m_title; }
    void setTitle(const QString& value) { m_title = value; }

    qint64 tickToMicroseconds(Tick tick) const;
    Tick microsecondsToTick(qint64 microseconds) const;
    qint64 playbackTickToMicroseconds(Tick outputTick) const;
    QVector<PlaybackSegment> playbackSegments() const;
    Tick playbackDuration() const;

private:
    QVector<Track> m_tracks;
    QVector<TempoChange> m_tempos;
    Tick m_duration = 0;
    QString m_title;
    QVector<Measure> m_measures;
    QVector<KeySignatureChange> m_keySignatures;
    QVector<MarkerEvent> m_markers;
    QVector<LyricEvent> m_lyrics;
    KeyContext m_keyContext;
    struct TempoSegment {
        Tick start = 0;
        Tick end = 0;
        qint64 startUs = 0;
        double bpm = 120.0;
    };
    mutable QVector<TempoSegment> m_tempoMap;
};

} // namespace midi_play::music
