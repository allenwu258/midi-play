#include "domain/music/musicanalysis.h"
#include "domain/visualization/activenotelookup.h"
#include "domain/visualization/playbackvisualizationprojector.h"
#include "domain/visualization/visiblenoteindex.h"
#include "presentation/playbackmetadatapresenter.h"
#include "presentation/visualization/scenelayoutengine.h"

#include <QCoreApplication>
#include <QDebug>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <algorithm>

namespace {

using midi_play::music::Measure;
using midi_play::music::MusicDocument;
using midi_play::music::NoteEvent;
using midi_play::music::Track;
using midi_play::presentation::visualization::SceneLayoutEngine;
using midi_play::presentation::PlaybackMetadataPresenter;
using midi_play::visualization::PlaybackVisualizationProjector;
using midi_play::visualization::ActiveNoteLookup;
using midi_play::visualization::VisibleNoteIndex;

void require(bool condition, const char* message)
{
    if (!condition) {
        std::fprintf(stderr, "FAILED: %s\n", message);
        std::exit(EXIT_FAILURE);
    }
}

MusicDocument repeatedDocument()
{
    MusicDocument document;
    document.setTitle(QStringLiteral("Projection test"));
    document.tempos().push_back({0, 120.0, 0});
    document.keySignatures().push_back({0, 0, QStringLiteral("major")});
    document.setDuration(960);

    Track track;
    track.id = QStringLiteral("piano");
    track.name = QStringLiteral("Piano");
    Measure first {1, 0, 480};
    first.repeatStart = true;
    Measure second {2, 480, 480};
    second.repeatEnd = true;
    second.repeatCount = 2;
    track.measures = {first, second};

    NoteEvent c;
    c.noteId = 1;
    c.start = 0;
    c.duration = 240;
    c.pitch = 60;
    c.velocity = 90;
    NoteEvent e = c;
    e.noteId = 2;
    e.start = 480;
    e.pitch = 64;
    track.notes = {c, e};

    document.tracks().push_back(track);
    document.rebuildMeasureGrid();
    midi_play::music::MusicAnalyzer().analyze(document);
    return document;
}

void testRepeatProjection()
{
    auto document = repeatedDocument();
    QString error;
    const auto chart = PlaybackVisualizationProjector().project(document, 7, {}, &error);
    require(chart != nullptr, "projector must create a chart");
    require(error.isEmpty(), "valid projection must not report an error");
    require(chart->generation() == 7, "chart generation must be preserved");
    require(chart->notes().size() == 4, "two notes in a repeated range must create four instances");
    require(chart->durationUs() == 2'000'000, "repeat-aware duration must be two seconds");
    require(chart->notes()[0].startUs == 0, "first pass first note timestamp");
    require(chart->notes()[1].startUs == 500'000, "first pass second note timestamp");
    require(chart->notes()[2].startUs == 1'000'000, "second pass first note timestamp");
    require(chart->notes()[3].startUs == 1'500'000, "second pass second note timestamp");
    require(chart->notes()[0].instanceId != chart->notes()[2].instanceId,
            "repeat instances require distinct identities");
    require(chart->notes()[0].simplifiedLabel == QStringLiteral("1"),
            "C in C major should be projected as degree 1");
}

void testVisibleIndex()
{
    auto document = repeatedDocument();
    const auto chart = PlaybackVisualizationProjector().project(document, 1);
    VisibleNoteIndex index(chart->notes());
    QVector<int> result;
    index.query(490'000, 520'000, result);
    require(result.size() == 1, "narrow window should return only the second note");
    require(chart->notes()[result.front()].pitch == 64, "visible note pitch must match query window");

    index.query(990'000, 1'020'000, result);
    require(result.size() == 1 && chart->notes()[result.front()].pitch == 60,
            "index must support non-incremental seek queries");

    index.query(2'100'000, 2'200'000, result);
    require(result.isEmpty(), "window after chart end must be empty");
}

void testActiveNoteLookup()
{
    ActiveNoteLookup lookup;
    lookup.reset(3);

    midi_play::visualization::VisualNote first;
    first.pitch = 60;
    first.simplifiedLabel = QStringLiteral("1");
    lookup.add(4, first);

    midi_play::visualization::VisualNote replacement = first;
    replacement.simplifiedLabel = QStringLiteral("1-high-priority");
    lookup.add(9, replacement);

    midi_play::visualization::VisualNote second;
    second.pitch = 64;
    second.simplifiedLabel = QStringLiteral("3");
    lookup.add(12, second);

    midi_play::visualization::VisualNote percussion;
    percussion.flags = midi_play::visualization::PercussionNote;
    percussion.drumLane = 1;
    lookup.add(15, percussion);

    midi_play::visualization::VisualNote invalidIndexNote = first;
    invalidIndexNote.pitch = 61;
    lookup.add(-1, invalidIndexNote);

    require(lookup.noteIndexForPitch(60) == 9,
            "later active note must retain reverse-scan keyboard priority");
    require(lookup.noteIndexForPitch(64) == 12, "second pitch must be indexed");
    require(lookup.noteIndexForPitch(61) == -1, "negative note index must be rejected");
    require(lookup.noteIndexForDrumLane(1) == 15, "active drum lane must be indexed");
    require(lookup.melodicLabelNoteIndices() == QVector<int>({4, 12}),
            "melodic labels must preserve first-pitch order and remove duplicates");
    require(lookup.noteIndexForPitch(-1) == -1 && lookup.noteIndexForPitch(128) == -1,
            "out-of-range MIDI pitches must be rejected");
    require(lookup.noteIndexForDrumLane(-1) == -1 && lookup.noteIndexForDrumLane(3) == -1,
            "out-of-range drum lanes must be rejected");

    lookup.reset(1);
    require(lookup.noteIndexForPitch(60) == -1 && lookup.noteIndexForDrumLane(0) == -1
                && lookup.melodicLabelNoteIndices().isEmpty(),
            "frame reset must clear all active-note lookup state");
}

void testSceneGeometry()
{
    auto document = repeatedDocument();
    const auto chart = PlaybackVisualizationProjector().project(document, 1);
    const auto geometry = SceneLayoutEngine().layout(QSizeF(1280.0, 720.0), chart.get(), 5'000'000);
    require(geometry.fallingRect.top() == 0.0,
            "falling field must start at the viewport top after removing the duplicate information bar");
    require(geometry.strikeLineY > geometry.fallingRect.top(), "strike line must be inside falling field");
    require(geometry.strikeLineY < geometry.keyboardRect.top(), "strike line must be above keyboard");
    require(geometry.pixelsPerMicrosecond > 0.0, "time scale must be positive");
    const auto* c = geometry.pitchSlot(60);
    const auto* cSharp = geometry.pitchSlot(61);
    const auto* d = geometry.pitchSlot(62);
    require(c && cSharp && d, "projected pitch range must contain test notes");
    require(!c->blackKey && cSharp->blackKey && !d->blackKey, "piano key classification");
    require(c->centerX < cSharp->centerX && cSharp->centerX < d->centerX,
            "pitch centers must be strictly ordered");
    require(cSharp->keyRect.width() < c->keyRect.width(), "black keys must be narrower than white keys");
    const qreal expected = (geometry.strikeLineY - geometry.fallingRect.top()) / 5'000'000.0;
    require(std::abs(geometry.pixelsPerMicrosecond - expected) < 1e-12,
            "time scale must derive from look-ahead and available height");
}

void testPlaybackMetadataPresentation()
{
    MusicDocument document;
    document.setTitle(QStringLiteral("Metadata timeline"));
    document.tempos().push_back({0, 90.0, 0});
    document.tempos().push_back({480, 140.0, 0});
    document.keySignatures().push_back({0, -3, QStringLiteral("minor")});
    document.keySignatures().push_back({480, 2, QStringLiteral("major")});
    document.setDuration(960);

    Track track;
    track.id = QStringLiteral("metadata");
    track.timeSignatures.push_back({0, 3, 4});
    track.timeSignatures.push_back({480, 6, 8});
    NoteEvent note;
    note.noteId = 1;
    note.start = 0;
    note.duration = 960;
    note.pitch = 60;
    track.notes.push_back(note);
    document.tracks().push_back(track);
    document.rebuildMeasureGrid();
    midi_play::music::MusicAnalyzer().analyze(document);

    const auto chart = PlaybackVisualizationProjector().project(document, 1);
    require(chart != nullptr, "metadata test chart must be projected");

    const auto initial = PlaybackMetadataPresenter::at(chart.get(), 0);
    require(initial.key == QStringLiteral("C min"), "initial key label must resolve from fifths and mode");
    require(initial.timeSignature == QStringLiteral("3/4"), "initial time signature label");
    require(initial.tempo == QStringLiteral("90 BPM"), "initial tempo label");

    const qint64 secondSectionUs = document.playbackTickToMicroseconds(480);
    const auto changed = PlaybackMetadataPresenter::at(chart.get(), secondSectionUs);
    require(changed.key == QStringLiteral("D maj"), "changed key label must follow the playback timeline");
    require(changed.timeSignature == QStringLiteral("6/8"), "changed time signature label");
    require(changed.tempo == QStringLiteral("140 BPM"), "changed tempo label");

    const auto empty = PlaybackMetadataPresenter::at(nullptr, 0);
    require(empty.key == QStringLiteral("--") && empty.timeSignature == QStringLiteral("--/--")
                && empty.tempo == QStringLiteral("-- BPM"),
            "empty metadata must use stable placeholders");
}

void testSimplifiedPitchSpelling()
{
    MusicDocument document;
    document.setTitle(QStringLiteral("Pitch spelling"));
    document.tempos().push_back({0, 120.0, 0});
    document.keySignatures().push_back({0, 0, QStringLiteral("major")});
    document.setDuration(480);
    Track track;
    track.id = QStringLiteral("pitch");
    for (int index = 0; index < 3; ++index) {
        NoteEvent note;
        note.noteId = index + 1;
        note.start = index * 120;
        note.duration = 90;
        note.pitch = 60 + index; // C, C#, D
        track.notes.push_back(note);
    }
    document.tracks().push_back(track);
    document.rebuildMeasureGrid();
    midi_play::music::MusicAnalyzer().analyze(document);
    const auto chart = PlaybackVisualizationProjector().project(document, 1);
    require(chart->notes()[0].simplifiedLabel == QStringLiteral("1"), "C spelling");
    require(chart->notes()[1].simplifiedLabel == QStringLiteral("#1"), "C sharp spelling");
    require(chart->notes()[2].simplifiedLabel == QStringLiteral("2"), "D must not be spelled as double sharp");

    MusicDocument sharpKeyDocument;
    sharpKeyDocument.tempos().push_back({0, 120.0, 0});
    sharpKeyDocument.keySignatures().push_back({0, 7, QStringLiteral("major")});
    sharpKeyDocument.setDuration(480);
    Track sharpTrack;
    sharpTrack.id = QStringLiteral("sharp-key");
    NoteEvent tonic;
    tonic.noteId = 1;
    tonic.start = 0;
    tonic.duration = 240;
    tonic.pitch = 61; // C# is scale degree 1 in C# major, not #1.
    sharpTrack.notes.push_back(tonic);
    sharpKeyDocument.tracks().push_back(sharpTrack);
    sharpKeyDocument.rebuildMeasureGrid();
    midi_play::music::MusicAnalyzer().analyze(sharpKeyDocument);
    const auto sharpChart = PlaybackVisualizationProjector().project(sharpKeyDocument, 1);
    require(sharpChart->notes().front().simplifiedLabel == QStringLiteral("1"),
            "accidentals must be relative to the active key");
}

void testLargeVisibleIndexAgainstFullScan()
{
    QVector<midi_play::visualization::VisualNote> notes;
    notes.reserve(10'000);
    for (int index = 0; index < 10'000; ++index) {
        midi_play::visualization::VisualNote note;
        note.instanceId = index + 1;
        note.startUs = static_cast<qint64>(index) * 37'000;
        note.keyEndUs = note.startUs + 20'000 + (index % 97) * 11'000;
        note.audibleEndUs = note.keyEndUs + (index % 19 == 0 ? 2'000'000 : 0);
        notes.push_back(note);
    }
    VisibleNoteIndex index(notes);
    QVector<int> actual;
    QVector<int> expected;
    for (qint64 position = 0; position < 370'000'000; position += 3'700'003) {
        const qint64 windowStart = position - 160'000;
        const qint64 windowEnd = position + 5'000'000;
        index.query(windowStart, windowEnd, actual);
        expected.clear();
        for (int noteIndex = 0; noteIndex < notes.size(); ++noteIndex) {
            if (notes[noteIndex].startUs <= windowEnd && notes[noteIndex].audibleEndUs >= windowStart) {
                expected.push_back(noteIndex);
            }
        }
        std::sort(actual.begin(), actual.end());
        require(actual == expected, "interval index must match full scan for arbitrary seek windows");
    }
}

} // namespace

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    testRepeatProjection();
    testVisibleIndex();
    testActiveNoteLookup();
    testSceneGeometry();
    testPlaybackMetadataPresentation();
    testSimplifiedPitchSpelling();
    testLargeVisibleIndexAgainstFullScan();
    std::fprintf(stdout, "visualization tests passed\n");
    return EXIT_SUCCESS;
}
