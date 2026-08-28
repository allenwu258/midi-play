#pragma once

#include <QMetaType>
#include <QString>
#include <QVector>

#include <cstdint>
#include <memory>

namespace midi_play::visualization {

using VisualTime = qint64;

struct ColorRgba {
    quint8 red = 255;
    quint8 green = 255;
    quint8 blue = 255;
    quint8 alpha = 255;
};

enum NoteVisualFlag : quint32 {
    NoNoteVisualFlags = 0,
    GraceNote = 1U << 0,
    GhostNote = 1U << 1,
    StaccatoNote = 1U << 2,
    AccentNote = 1U << 3,
    TenutoNote = 1U << 4,
    MarcatoNote = 1U << 5,
    TremoloNote = 1U << 6,
    TiedNote = 1U << 7,
    PercussionNote = 1U << 8
};

struct VisualNote {
    quint64 instanceId = 0;
    quint64 sourceNoteId = 0;
    quint64 chordInstanceId = 0;
    quint64 sourceChordId = 0;
    quint64 tieGroupId = 0;
    int trackIndex = -1;
    int pitch = 60;
    int velocity = 90;
    int channel = 0;
    int staff = 1;
    int voice = 1;
    int sourceMeasureIndex = -1;
    int repeatPass = 0;
    int scaleDegree = 0;
    int accidental = 0;
    int octaveOffset = 0;
    int drumLane = -1;
    int coincidentIndex = 0;
    int coincidentCount = 1;
    VisualTime startUs = 0;
    VisualTime keyEndUs = 0;
    VisualTime audibleEndUs = 0;
    quint32 flags = NoNoteVisualFlags;
    QString simplifiedLabel;

    bool isPercussion() const { return (flags & PercussionNote) != 0; }
    bool isGhost() const { return (flags & GhostNote) != 0; }
};

struct VisualTrack {
    QString id;
    QString name;
    ColorRgba color;
    bool percussion = false;
    int channel = 0;
    int program = 0;
    QVector<int> noteIndices;
};

struct VisualGridLine {
    VisualTime timeUs = 0;
    int measureNumber = 0;
    int beatIndex = 0;
    bool measureStart = false;
    QString measureLabel;
};

enum class MarkerKind {
    Text,
    Navigation,
    Tempo,
    KeySignature,
    TimeSignature
};

struct VisualMarker {
    quint64 instanceId = 0;
    VisualTime timeUs = 0;
    QString text;
    MarkerKind kind = MarkerKind::Text;
};

struct VisualLyric {
    quint64 instanceId = 0;
    VisualTime timeUs = 0;
    QString text;
    int verse = 1;
};

struct VisualTempoPoint {
    VisualTime timeUs = 0;
    double bpm = 120.0;
};

struct VisualKeyPoint {
    VisualTime timeUs = 0;
    int fifths = 0;
    QString mode = QStringLiteral("major");
};

struct VisualTimeSignaturePoint {
    VisualTime timeUs = 0;
    int beats = 4;
    int beatType = 4;
};

struct VisualDrumLane {
    int lane = -1;
    int pitch = 0;
    QString name;
};

struct PitchRange {
    int minimum = 48;
    int maximum = 71;
};

class PlaybackVisualizationProjector;

class VisualChart final {
public:
    const QString& title() const { return m_title; }
    VisualTime durationUs() const { return m_durationUs; }
    const QVector<VisualTrack>& tracks() const { return m_tracks; }
    const QVector<VisualNote>& notes() const { return m_notes; }
    const QVector<int>& noteIndicesByEnd() const { return m_noteIndicesByEnd; }
    const QVector<VisualGridLine>& gridLines() const { return m_gridLines; }
    const QVector<VisualMarker>& markers() const { return m_markers; }
    const QVector<VisualLyric>& lyrics() const { return m_lyrics; }
    const QVector<VisualTempoPoint>& tempos() const { return m_tempos; }
    const QVector<VisualKeyPoint>& keys() const { return m_keys; }
    const QVector<VisualTimeSignaturePoint>& timeSignatures() const { return m_timeSignatures; }
    const QVector<VisualDrumLane>& drumLanes() const { return m_drumLanes; }
    PitchRange pitchRange() const { return m_pitchRange; }
    quint64 generation() const { return m_generation; }

    double tempoAt(VisualTime timeUs) const;
    VisualKeyPoint keyAt(VisualTime timeUs) const;
    VisualTimeSignaturePoint timeSignatureAt(VisualTime timeUs) const;
    QString markerAt(VisualTime timeUs) const;
    QString lyricAt(VisualTime timeUs) const;

private:
    friend class PlaybackVisualizationProjector;

    QString m_title;
    VisualTime m_durationUs = 0;
    QVector<VisualTrack> m_tracks;
    QVector<VisualNote> m_notes;
    QVector<int> m_noteIndicesByEnd;
    QVector<VisualGridLine> m_gridLines;
    QVector<VisualMarker> m_markers;
    QVector<VisualLyric> m_lyrics;
    QVector<VisualTempoPoint> m_tempos;
    QVector<VisualKeyPoint> m_keys;
    QVector<VisualTimeSignaturePoint> m_timeSignatures;
    QVector<VisualDrumLane> m_drumLanes;
    PitchRange m_pitchRange;
    quint64 m_generation = 0;
};

using VisualChartPtr = std::shared_ptr<const VisualChart>;

} // namespace midi_play::visualization

Q_DECLARE_METATYPE(midi_play::visualization::VisualChartPtr)
