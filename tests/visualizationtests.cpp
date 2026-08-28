#include "domain/music/musicanalysis.h"
#include "domain/visualization/activenotelookup.h"
#include "domain/visualization/playbackvisualizationprojector.h"
#include "domain/visualization/visiblenoteindex.h"
#include "domain/visualization/visiblenotewindowcache.h"
#include "presentation/playbackmetadatapresenter.h"
#include "presentation/visualization/playbackoverlaytimeline.h"
#include "presentation/visualization/rasterrenderpolicy.h"
#include "presentation/visualization/noterendercache.h"
#include "presentation/visualization/scenelayoutengine.h"
#include "presentation/visualization/textlayoutcache.h"

#include <QDebug>
#include <QGuiApplication>
#include <QImage>
#include <QPainter>

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
using midi_play::presentation::visualization::NoteRenderCache;
using midi_play::presentation::visualization::RasterRenderPolicy;
using midi_play::presentation::visualization::PlaybackOverlayTimeline;
using midi_play::presentation::visualization::TextLayoutCache;
using midi_play::presentation::visualization::TextLayoutRole;
using midi_play::presentation::PlaybackMetadataPresenter;
using midi_play::presentation::PlaybackMetadataTimeline;
using midi_play::visualization::PlaybackVisualizationProjector;
using midi_play::visualization::ActiveNoteLookup;
using midi_play::visualization::VisibleNoteIndex;
using midi_play::visualization::VisibleNoteWindowCache;

void require(bool condition, const char* message)
{
    if (!condition) {
        std::fprintf(stderr, "FAILED: %s\n", message);
        std::exit(EXIT_FAILURE);
    }
}

QColor compositeOverOpaqueForTest(const QColor& foreground, const QColor& background)
{
    const int alpha = foreground.alpha();
    const int inverseAlpha = 255 - alpha;
    const auto channel = [alpha, inverseAlpha](int foregroundValue, int backgroundValue) {
        return (foregroundValue * alpha + backgroundValue * inverseAlpha + 127) / 255;
    };
    return QColor(channel(foreground.red(), background.red()),
                  channel(foreground.green(), background.green()),
                  channel(foreground.blue(), background.blue()), 255);
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

void testVisibleNoteWindowCache()
{
    QVector<midi_play::visualization::VisualNote> notes;
    for (int index = 0; index < 5; ++index) {
        midi_play::visualization::VisualNote note;
        note.startUs = index * 100;
        note.keyEndUs = note.startUs + 40;
        note.audibleEndUs = note.keyEndUs;
        notes.push_back(note);
    }
    midi_play::visualization::VisualNote sustained;
    sustained.startUs = 0;
    sustained.keyEndUs = 20;
    sustained.audibleEndUs = 1'000;
    notes.insert(1, sustained);

    VisibleNoteIndex index(notes);
    VisibleNoteWindowCache cache;
    require(cache.ensure(index, 100, 200, 50), "first visible window must query the index");
    require(cache.queryCount() == 1 && cache.cachedStartUs() == 50 && cache.cachedEndUs() == 250,
            "first query must expand both sides by the configured guard");
    require(!cache.ensure(index, 120, 220, 50) && cache.queryCount() == 1,
            "normal forward frames inside the guard must reuse candidates");
    require(!cache.ensure(index, 80, 180, 50) && cache.queryCount() == 1,
            "small backward motion inside the guard must reuse candidates");

    for (int frame = 0; frame < 60; ++frame) {
        const qint64 startUs = 80 + frame * 2;
        cache.ensure(index, startUs, startUs + 100, 50);
    }
    require(cache.queryCount() == 2,
            "sixty small frame advances must refresh only when the exact window crosses the guard");

    require(cache.ensure(index, 900, 950, 50) && cache.queryCount() == 3,
            "an arbitrary seek outside the candidate window must refresh immediately");
    require(cache.candidateNoteIndices().contains(1),
            "a note with an old attack and a long audible tail must remain a candidate");

    const quint64 revision = index.revision();
    index.rebuild(notes);
    require(index.revision() == revision + 1, "index rebuild must advance its revision");
    require(cache.ensure(index, 900, 950, 50) && cache.queryCount() == 4,
            "index revision changes must invalidate a contained candidate window");

    require(cache.ensure(index, 20, 10, 50) && !cache.isValid(),
            "an invalid exact window must reset the candidate cache");
    require(cache.queryCount() == 4, "invalid windows must not execute interval queries");

    require(cache.ensure(index, 140, 200, 0), "zero-guard boundary query must refresh");
    require(cache.candidateNoteIndices().contains(2),
            "notes ending exactly at the inclusive window start must be retained");
    require(cache.candidateNoteIndices().contains(3),
            "notes starting exactly at the inclusive window end must be retained");
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
    require(lookup.activePitches() == QVector<int>({60, 64}),
            "active pitch iteration must be unique and preserve discovery order");
    require(lookup.activeDrumLanes() == QVector<int>({1}),
            "active drum iteration must contain each lane once");
    require(lookup.melodicLabelNoteIndices() == QVector<int>({4, 12}),
            "melodic labels must preserve first-pitch order and remove duplicates");
    require(lookup.noteIndexForPitch(-1) == -1 && lookup.noteIndexForPitch(128) == -1,
            "out-of-range MIDI pitches must be rejected");
    require(lookup.noteIndexForDrumLane(-1) == -1 && lookup.noteIndexForDrumLane(3) == -1,
            "out-of-range drum lanes must be rejected");

    lookup.reset(1);
    require(lookup.noteIndexForPitch(60) == -1 && lookup.noteIndexForDrumLane(0) == -1
                && lookup.activePitches().isEmpty() && lookup.activeDrumLanes().isEmpty()
                && lookup.melodicLabelNoteIndices().isEmpty(),
            "frame reset must clear all active-note lookup state");
}

void testNoteRenderCache()
{
    auto document = repeatedDocument();
    const auto chart = PlaybackVisualizationProjector().project(document, 1);
    const auto geometry = SceneLayoutEngine().layout(QSizeF(960.0, 640.0), chart.get(), 5'000'000);

    NoteRenderCache cache;
    cache.prepare(chart, geometry);
    require(cache.chartBuildCount() == 1 && cache.geometryBuildCount() == 1,
            "initial render preparation must build chart and geometry caches once");
    require(cache.notes().size() == chart->notes().size(),
            "render cache must keep one direct record per projected note");
    require(cache.styles().size() == 1,
            "identical repeated notes must share one immutable render style");
    require(cache.note(0) && cache.note(0)->validGeometry && cache.note(0)->width > 0.0,
            "render cache must precompute valid horizontal note geometry");
    require(cache.note(0)->styleIndex == cache.note(2)->styleIndex,
            "repeat instances with equal style inputs must reuse the same style");

    cache.prepare(chart, geometry);
    require(cache.chartBuildCount() == 1 && cache.geometryBuildCount() == 1,
            "stable chart and layout must not rebuild render caches per frame");
    const auto resizedGeometry = SceneLayoutEngine().layout(
        QSizeF(1'080.0, 640.0), chart.get(), 5'000'000);
    cache.prepare(chart, resizedGeometry);
    require(cache.chartBuildCount() == 1 && cache.geometryBuildCount() == 2,
            "resize must rebuild only horizontal geometry, not immutable styles");
}

void testNoteRenderCacheStaticFlags()
{
    MusicDocument document;
    document.tempos().push_back({0, 120.0, 0});
    document.setDuration(480);
    Track track;
    track.id = QStringLiteral("render-flags");
    NoteEvent note;
    note.noteId = 1;
    note.start = 0;
    note.duration = 240;
    note.pitch = 60;
    note.velocity = 72;
    note.ghost = true;
    note.tremolo = true;
    track.notes.push_back(note);
    track.controlChanges.push_back({0, 0, 64, 127, 0});
    track.controlChanges.push_back({400, 0, 64, 0, 1});
    document.tracks().push_back(track);
    document.rebuildMeasureGrid();
    midi_play::music::MusicAnalyzer().analyze(document);

    const auto chart = PlaybackVisualizationProjector().project(document, 1);
    const auto geometry = SceneLayoutEngine().layout(QSizeF(800.0, 600.0), chart.get(), 5'000'000);
    NoteRenderCache cache;
    cache.prepare(chart, geometry);

    const auto* prepared = cache.note(0);
    const auto* style = cache.styleForNote(0);
    require(prepared && prepared->hasTail && prepared->tremolo,
            "tail and tremolo flags must be resolved once during chart preparation");
    require(style && style->fillBrush.color().alpha() == 120,
            "ghost alpha must be precomputed in the shared fill style");
    require(style && style->activeWhiteKeyBrush.color().alpha() == 255
                && style->activeBlackKeyBrush.color().alpha() == 255
                && style->activeDrumKeyBrush.color().alpha() == 255,
            "active key brushes must be opaque final colors independent of cached key pixels");
    require(style->activeWhiteKeyBrush.color()
                == compositeOverOpaqueForTest(style->fillBrush.color(), QColor("#101214"))
                && style->activeDrumKeyBrush.color() == style->activeWhiteKeyBrush.color(),
            "white and drum highlights must resolve against the keyboard background");
    require(style->activeBlackKeyBrush.color()
                == compositeOverOpaqueForTest(style->fillBrush.color(), QColor("#dedfd9")),
            "black-key highlights must resolve against the underlying white-key layer");
    require(style->inactiveBorderPen.style() == Qt::DashLine
                && style->activeBorderPen.style() == Qt::DashLine,
            "ghost border pens must be precomputed for both transport states");
}

void testNoteRenderCacheDeduplicatesResolvedVelocityColor()
{
    MusicDocument document;
    document.tempos().push_back({0, 120.0, 0});
    document.setDuration(480);
    Track track;
    track.id = QStringLiteral("render-velocity-buckets");
    NoteEvent first;
    first.noteId = 1;
    first.start = 0;
    first.duration = 120;
    first.pitch = 60;
    first.velocity = 1;
    NoteEvent second = first;
    second.noteId = 2;
    second.start = 240;
    second.velocity = 2;
    track.notes = {first, second};
    document.tracks().push_back(track);
    document.rebuildMeasureGrid();
    midi_play::music::MusicAnalyzer().analyze(document);

    const auto chart = PlaybackVisualizationProjector().project(document, 1);
    const auto geometry = SceneLayoutEngine().layout(QSizeF(800.0, 600.0), chart.get(), 5'000'000);
    NoteRenderCache cache;
    cache.prepare(chart, geometry);

    require(cache.styles().size() == 1
                && cache.note(0)->styleIndex == cache.note(1)->styleIndex,
            "velocities resolving to the same color must share one render style");
}

void testRasterRenderPolicy()
{
    QImage image(8, 8, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::black);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    painter.setRenderHint(QPainter::TextAntialiasing, false);

    RasterRenderPolicy::apply(painter);

    require(!painter.testRenderHint(QPainter::Antialiasing),
            "raster geometry antialiasing must be disabled");
    require(!painter.testRenderHint(QPainter::SmoothPixmapTransform),
            "unused smooth image transforms must be disabled");
    require(painter.testRenderHint(QPainter::TextAntialiasing),
            "text antialiasing must remain enabled independently");

    painter.setPen(Qt::NoPen);
    painter.setBrush(Qt::white);
    painter.drawRect(QRectF(1.25, 1.25, 4.5, 4.5));
    painter.end();

    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            const QRgb pixel = image.pixel(x, y);
            require(pixel == qRgb(0, 0, 0) || pixel == qRgb(255, 255, 255),
                    "aliased rectangle must not generate blended edge pixels");
        }
    }
}

void testRasterRenderPolicyIsScopedByPainterState()
{
    QImage image(8, 8, QImage::Format_ARGB32_Premultiplied);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, false);

    painter.save();
    RasterRenderPolicy::apply(painter);
    painter.restore();

    require(painter.testRenderHint(QPainter::Antialiasing),
            "restoring painter state must recover the caller's geometry hint");
    require(!painter.testRenderHint(QPainter::TextAntialiasing),
            "restoring painter state must recover the caller's text hint");
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

    PlaybackMetadataTimeline timeline;
    timeline.setChart(chart);
    midi_play::presentation::PlaybackMetadata metadata;
    require(timeline.update(0, metadata), "first metadata timeline read must publish labels");
    require(!timeline.update(100'000, metadata),
            "positions inside one metadata segment must not rebuild or republish labels");
    require(timeline.update(secondSectionUs, metadata) && metadata.tempo == QStringLiteral("140 BPM"),
            "metadata boundary must publish the preformatted next segment");
    require(timeline.update(0, metadata) && metadata.tempo == QStringLiteral("90 BPM"),
            "reverse seeks must locate the earlier segment correctly");

    timeline.clear();
    require(timeline.update(0, metadata), "cleared metadata timeline must publish placeholders once");
    require(!timeline.update(1'000'000, metadata),
            "empty metadata timeline must not republish stable placeholders");
}

void testMetadataTimelineMergesEquivalentDisplaySegments()
{
    MusicDocument document;
    document.tempos() = {{0, 120.1, 0}, {240, 120.4, 1}, {480, 121.0, 2}};
    document.setDuration(960);
    Track track;
    track.id = QStringLiteral("metadata-merge");
    NoteEvent note;
    note.noteId = 1;
    note.start = 0;
    note.duration = 960;
    track.notes.push_back(note);
    document.tracks().push_back(track);
    document.rebuildMeasureGrid();
    midi_play::music::MusicAnalyzer().analyze(document);

    const auto chart = PlaybackVisualizationProjector().project(document, 1);
    PlaybackMetadataTimeline timeline;
    timeline.setChart(chart);
    require(timeline.segmentCount() == 2,
            "change points with identical display metadata must collapse into one segment");
}

void testPlaybackOverlayTimeline()
{
    MusicDocument document;
    document.tempos().push_back({0, 120.0, 0});
    document.setDuration(960);
    document.markers() = {
        {0, QStringLiteral("Intro"), 0},
        {480, QStringLiteral("Verse"), 1}
    };
    document.lyrics() = {
        {240, QStringLiteral("first"), 1, 0},
        {480, QStringLiteral("second"), 1, 1}
    };
    Track track;
    track.id = QStringLiteral("overlay");
    NoteEvent note;
    note.noteId = 1;
    note.start = 0;
    note.duration = 960;
    track.notes.push_back(note);
    document.tracks().push_back(track);
    document.rebuildMeasureGrid();
    midi_play::music::MusicAnalyzer().analyze(document);

    const auto chart = PlaybackVisualizationProjector().project(document, 1);
    PlaybackOverlayTimeline timeline;
    timeline.setChart(chart);
    require(timeline.segmentCount() == 3,
            "marker and lyric change points must be merged into a compact overlay timeline");

    const auto& initial = timeline.at(0);
    require(initial.marker == QStringLiteral("Intro") && initial.lyric.isEmpty(),
            "overlay timeline must apply events at time zero");
    const quint64 firstSearchCount = timeline.binarySearchCount();
    for (qint64 positionUs = 1'000; positionUs < 240'000; positionUs += 2'000) {
        timeline.at(positionUs);
    }
    require(timeline.binarySearchCount() == firstSearchCount,
            "stable overlay playback must use segment bounds without repeated binary searches");

    const auto& firstLyric = timeline.at(250'000);
    require(firstLyric.marker == QStringLiteral("Intro")
                && firstLyric.lyric == QStringLiteral("first"),
            "lyric boundary must preserve the current marker");
    const auto& second = timeline.at(500'000);
    require(second.marker == QStringLiteral("Verse")
                && second.lyric == QStringLiteral("second"),
            "coincident marker and lyric events must update atomically");
    const auto& reverse = timeline.at(10'000);
    require(reverse.marker == QStringLiteral("Intro") && reverse.lyric.isEmpty(),
            "reverse seek must resolve the earlier overlay segment");
}

void testTextLayoutCache()
{
    TextLayoutCache cache(2);
    QFont font;
    font.setPointSizeF(9.5);

    const auto& first = cache.layout(TextLayoutRole::Marker, QStringLiteral("Section A"), font);
    require(first.displayText == QStringLiteral("Section A") && cache.buildCount() == 1,
            "first text request must shape and cache one layout");
    cache.layout(TextLayoutRole::Marker, QStringLiteral("Section A"), font);
    cache.layout(TextLayoutRole::Marker, QStringLiteral("Section A"), font, -1.0, 1.001);
    require(cache.buildCount() == 1,
            "identical and sub-quantum DPR requests must reuse the prepared glyph layout");

    cache.layout(TextLayoutRole::Lyric, QStringLiteral("A long lyric"), font, 30.0);
    require(cache.size() == 2 && cache.buildCount() == 2,
            "width and role must participate in the layout cache key");
    cache.layout(TextLayoutRole::Octave, QStringLiteral("C4"), font);
    require(cache.size() == 2 && cache.buildCount() == 3,
            "bounded cache must evict one least-recently-used entry on overflow");
    cache.layout(TextLayoutRole::Marker, QStringLiteral("Section A"), font);
    require(cache.buildCount() == 4,
            "requesting an evicted layout must rebuild it exactly once");
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
    QGuiApplication app(argc, argv);
    testRepeatProjection();
    testVisibleIndex();
    testVisibleNoteWindowCache();
    testActiveNoteLookup();
    testNoteRenderCache();
    testNoteRenderCacheStaticFlags();
    testNoteRenderCacheDeduplicatesResolvedVelocityColor();
    testRasterRenderPolicy();
    testRasterRenderPolicyIsScopedByPainterState();
    testSceneGeometry();
    testPlaybackMetadataPresentation();
    testMetadataTimelineMergesEquivalentDisplaySegments();
    testPlaybackOverlayTimeline();
    testTextLayoutCache();
    testSimplifiedPitchSpelling();
    testLargeVisibleIndexAgainstFullScan();
    std::fprintf(stdout, "visualization tests passed\n");
    return EXIT_SUCCESS;
}
