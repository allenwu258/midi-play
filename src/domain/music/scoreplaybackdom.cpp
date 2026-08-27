#include "scoreplaybackdom.h"

#include <QHash>
#include <algorithm>

namespace midi_play::music {

std::shared_ptr<const ScorePlaybackDom> ScorePlaybackDom::build(const MusicDocument& document)
{
    auto dom = std::make_shared<ScorePlaybackDom>();
    int nextNoteId = 0;
    int nextChordId = 0;
    int nextRestId = 0;

    for (const auto& track : document.tracks()) {
        ScoreDomPart part;
        part.id = track.id;
        part.name = track.name;
        const auto& measures = track.measures.isEmpty() ? document.measures() : track.measures;

        QHash<int, QVector<const NoteEvent*>> notesByStaff;
        for (const auto& note : track.notes) {
            notesByStaff[note.staff].push_back(&note);
        }

        QVector<int> staffNumbers = notesByStaff.keys().toVector();
        for (const auto& measure : measures) {
            if (!staffNumbers.contains(1)) staffNumbers.push_back(1);
            Q_UNUSED(measure);
        }
        std::sort(staffNumbers.begin(), staffNumbers.end());

        for (const int staffNumber : staffNumbers) {
            ScoreDomStaff staff;
            staff.number = staffNumber;
            const auto notes = notesByStaff.value(staffNumber);
            for (const auto& measure : measures) {
                ScoreDomMeasure domMeasure;
                domMeasure.value = measure;
                QHash<Tick, QVector<const NoteEvent*>> notesAtTick;
                for (const auto* note : notes) {
                    if (note->start >= measure.start
                        && note->start < measure.start + measure.duration) {
                        notesAtTick[note->start].push_back(note);
                    }
                }
                QVector<Tick> ticks = notesAtTick.keys().toVector();
                std::sort(ticks.begin(), ticks.end());
                for (const Tick tick : ticks) {
                    ScoreDomSegment segment;
                    segment.tick = tick;
                    QHash<int, int> chordByVoice;
                    for (const auto* note : notesAtTick.value(tick)) {
                        ScoreDomNote domNote;
                        domNote.id = nextNoteId++;
                        domNote.value = *note;
                        dom->m_notes.push_back(domNote);
                        if (note->rest) {
                            ScoreDomRest rest;
                            rest.id = nextRestId++;
                            rest.tick = note->start;
                            rest.duration = note->duration;
                            rest.voice = note->voice;
                            dom->m_rests.push_back(rest);
                            segment.restIds.push_back(rest.id);
                            continue;
                        }

                        const int voice = note->voice;
                        int chordId = chordByVoice.value(voice, -1);
                        if (chordId < 0) {
                            ScoreDomChord chord;
                            chord.id = nextChordId++;
                            chord.tick = note->start;
                            chord.duration = note->duration;
                            chord.voice = voice;
                            dom->m_chords.push_back(chord);
                            chordId = chord.id;
                            chordByVoice.insert(voice, chordId);
                            segment.chordIds.push_back(chordId);
                        }
                        auto chordIt = std::find_if(dom->m_chords.begin(), dom->m_chords.end(),
                                                    [chordId](const auto& chord) { return chord.id == chordId; });
                        if (chordIt != dom->m_chords.end()) chordIt->noteIds.push_back(domNote.id);
                    }
                    if (!segment.chordIds.isEmpty() || !segment.restIds.isEmpty()) {
                        domMeasure.segments.push_back(std::move(segment));
                    }
                }
                staff.measures.push_back(std::move(domMeasure));
            }
            part.staves.push_back(std::move(staff));
        }

        for (const auto& hairpin : track.hairpins) {
            dom->m_spanners.push_back({hairpin.crescendo ? QStringLiteral("crescendo")
                                                        : QStringLiteral("diminuendo"),
                                       hairpin.start, hairpin.end, 1});
        }
        dom->m_parts.push_back(std::move(part));
    }
    return dom;
}

} // namespace midi_play::music
