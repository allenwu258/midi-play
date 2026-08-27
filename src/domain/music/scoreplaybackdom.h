#pragma once

#include "musicdocument.h"

#include <QVector>
#include <memory>

namespace midi_play::music {

struct ScoreDomNote {
    int id = -1;
    NoteEvent value;
};

struct ScoreDomChord {
    int id = -1;
    Tick tick = 0;
    Tick duration = 0;
    int voice = 1;
    QVector<int> noteIds;
};

struct ScoreDomRest {
    int id = -1;
    Tick tick = 0;
    Tick duration = 0;
    int voice = 1;
};

struct ScoreDomSegment {
    Tick tick = 0;
    QVector<int> chordIds;
    QVector<int> restIds;
};

struct ScoreDomMeasure {
    Measure value;
    QVector<ScoreDomSegment> segments;
};

struct ScoreDomStaff {
    int number = 1;
    QVector<ScoreDomMeasure> measures;
};

struct ScoreDomPart {
    QString id;
    QString name;
    QVector<ScoreDomStaff> staves;
};

struct ScoreDomSpanner {
    QString type;
    Tick start = 0;
    Tick end = 0;
    int staff = 1;
};

// Immutable playback-oriented projection of the score hierarchy. Readers
// can remain lightweight while playback and future notation rendering share
// stable Part/Staff/Measure/Segment/Chord/Rest relationships.
class ScorePlaybackDom final {
public:
    static std::shared_ptr<const ScorePlaybackDom> build(const MusicDocument& document);

    const QVector<ScoreDomPart>& parts() const { return m_parts; }
    const QVector<ScoreDomNote>& notes() const { return m_notes; }
    const QVector<ScoreDomChord>& chords() const { return m_chords; }
    const QVector<ScoreDomRest>& rests() const { return m_rests; }
    const QVector<ScoreDomSpanner>& spanners() const { return m_spanners; }

private:
    QVector<ScoreDomPart> m_parts;
    QVector<ScoreDomNote> m_notes;
    QVector<ScoreDomChord> m_chords;
    QVector<ScoreDomRest> m_rests;
    QVector<ScoreDomSpanner> m_spanners;
};

} // namespace midi_play::music
