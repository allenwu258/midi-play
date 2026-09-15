#include "domain/music/musicanalysis.h"
#include "domain/playback/playbackmodel.h"
#include "domain/visualization/playbackvisualizationprojector.h"
#include "domain/visualization/visiblenoteindex.h"
#include "infrastructure/midi/mididocumentbuilder.h"
#include "infrastructure/midi/midinormalizer.h"
#include "presentation/visualization/fallingnotesrenderer.h"
#include "presentation/visualization/noteframestate.h"
#include "presentation/visualization/scenelayoutengine.h"
#include "presentation/visualization/visualplaybackclock.h"
#if MIDI_PLAY_HAS_VULKAN
#include "presentation/visualization/fallingnotesvulkanwindow.h"
#include <QVulkanInstance>
#endif

#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QPainter>
#include <QThread>
#include <algorithm>
#include <cstdio>
#include <cstdlib>

namespace {
using namespace midi_play;
using namespace midi_play::presentation::visualization;

void require(bool condition, const char* message)
{
    if (!condition) { std::fprintf(stderr, "FAILED: %s\n", message); std::exit(EXIT_FAILURE); }
}

midi::MidiRawEvent event(midi::MidiMessageKind kind, music::Tick tick, int data1, int data2 = 0)
{
    midi::MidiRawEvent result;
    result.kind = kind;
    result.tick = tick;
    result.data1 = data1;
    result.data2 = data2;
    return result;
}

std::shared_ptr<music::MusicDocument> importEvents(QVector<midi::MidiRawEvent> events)
{
    midi::MidiParsedFile source;
    source.header.trackCount = 1;
    source.tracks.resize(1);
    for (int i = 0; i < events.size(); ++i) events[i].sequence = i;
    source.tracks[0].events = std::move(events);
    const auto normalized = midi::MidiNormalizer().normalize(source);
    require(normalized.ok(), "MIDI normalization must succeed");
    const auto document = midi::MidiDocumentBuilder().build(*normalized.file);
    require(document.ok(), "MIDI document creation must succeed");
    return document.document;
}

void testPedalTimingSurvivesImport()
{
    using K = midi::MidiMessageKind;
    const auto document = importEvents({event(K::ControlChange, 0, 64, 127),
        event(K::NoteOn, 0, 60, 96), event(K::NoteOff, 288, 60),
        event(K::NoteOn, 480, 60, 96), event(K::NoteOff, 768, 60),
        event(K::ControlChange, 2880, 64, 0)});
    const auto chart = visualization::PlaybackVisualizationProjector().project(*document, 1);
    require(chart->notes().size() == 2, "pedal repetitions must remain separate attacks");
    require(chart->notes()[0].keyEndUs == 300'000 && chart->notes()[0].audibleEndUs == 3'000'000,
            "short key hold must have a separate three-second pedal tail");
    require(chart->notes()[1].coincidentCount == 1,
            "pedal tails must not move subsequent attacks into narrow overlap lanes");
    playback::PlaybackModel model(document);
    bool correctOff = false;
    for (const auto& track : model.tracks()) for (const auto& note : track.events)
        if (note.kind == playback::PlaybackEventKind::NoteOff && note.timestampUs == 300'000) correctOff = true;
    require(correctOff, "audio must release the key at the same time as the visual body");
}

void testSostenutoCapturesOnlyHeldInstances()
{
    using K = midi::MidiMessageKind;
    const auto document = importEvents({event(K::NoteOn, 0, 60, 90),
        event(K::ControlChange, 60, 66, 127), event(K::ControlChange, 70, 64, 127),
        event(K::NoteOn, 90, 64, 90), event(K::NoteOff, 120, 60), event(K::NoteOff, 150, 64),
        event(K::ControlChange, 240, 64, 0), event(K::ControlChange, 480, 66, 0)});
    require(document->tracks()[0].notes[0].sustainEnd == 480,
            "sostenuto must retain the note captured at pedal press");
    require(document->tracks()[0].notes[1].sustainEnd == 240,
            "uncaptured notes must release with sustain even while sostenuto stays down");
}

music::MusicDocument basicDocument()
{
    music::MusicDocument document;
    document.setDuration(5760);
    document.tempos().push_back({0, 120.0, 0});
    music::Track track;
    track.id = QStringLiteral("piano");
    document.tracks().push_back(track);
    return document;
}

void testContinuousBarsAndPolyphonicTies()
{
    auto document = basicDocument();
    auto& track = document.tracks()[0];
    track.measures = {{1, 0, 480}, {2, 480, 480}};
    for (int pitch : {60, 64}) {
        music::NoteEvent note;
        note.pitch = pitch;
        note.duration = 480;
        note.tieStart = true;
        track.notes.push_back(note);
        note.start = 480;
        note.tieStart = false;
        note.tieStop = true;
        track.notes.push_back(note);
    }
    document.rebuildMeasureGrid();
    music::MusicAnalyzer().analyze(document);
    const auto chart = visualization::PlaybackVisualizationProjector().project(document, 1);
    require(chart->notes().size() == 2 && chart->notes()[0].keyEndUs == 1'000'000
            && chart->notes()[1].keyEndUs == 1'000'000, "polyphonic ties must merge across adjacent bars");
    track.notes.clear();
    music::NoteEvent note;
    note.start = 240;
    note.duration = 720;
    track.notes.push_back(note);
    const auto crossBar = visualization::PlaybackVisualizationProjector().project(document, 2);
    require(crossBar->notes()[0].keyEndUs == 1'000'000,
            "a continuous bar boundary must not truncate a held note");
}

void testGeometryAndMaterials()
{
    auto document = basicDocument();
    for (int pitch = 48; pitch <= 84; ++pitch) {
        music::NoteEvent note;
        note.pitch = pitch;
        note.duration = 3840;
        document.tracks()[0].notes.push_back(note);
    }
    music::NoteEvent overlap = document.tracks()[0].notes[0];
    overlap.start = 480;
    document.tracks()[0].notes.push_back(overlap);
    document.rebuildMeasureGrid();
    const auto chart = visualization::PlaybackVisualizationProjector().project(document, 1);
    const auto geometry = SceneLayoutEngine().layout({1280, 720}, chart.get(), 5'000'000);
    NoteRenderCache cache;
    cache.prepare(chart, geometry);
    const auto* low = cache.styleForNote(0);
    const auto* high = cache.styleForNote(36);
    require(low && high && low->material.body != high->material.body,
            "single-track registers must have stable perceptual color variation");
    require(low->material.head.alphaF() > low->material.body.alphaF()
            && low->material.tail.alphaF() < low->material.body.alphaF(),
            "attack/body/pedal hierarchy must remain explicit");
    require(chart->notes()[0].coincidentCount == 2 && chart->notes().back().coincidentCount == 2,
            "different-onset overlapping key holds must share stable lanes");
    for (int pitch = 49; pitch < 84; ++pitch) {
        const auto* a = geometry.pitchSlot(pitch);
        const auto* b = geometry.pitchSlot(pitch + 1);
        require(a->centerX + a->noteWidth * .5 < b->centerX - b->noteWidth * .5,
                "adjacent black/white note bodies must leave a visible gap");
    }
}

void testVisualClock()
{
    VisualPlaybackClock clock;
    clock.setPlaying(true);
    clock.sample(500'000, 5'000'000, 1'000'000);
    require(clock.position(1'010'000) == 510'000, "clock must interpolate between transport samples");
    clock.setRate(200, 1'000'000);
    require(clock.position(1'010'000) == 520'000, "clock must respect playback rate");
    require(clock.position(2'000'000) == 700'000, "clock must bound extrapolation when samples stop");
    clock.setPlaying(false);
    require(clock.position(2'000'000) == 500'000, "pause must use the authoritative endpoint");
    clock.sample(3'000'000, 5'000'000);
    clock.setPlaying(true);
    require(clock.position(8'000'000) == 3'000'000, "untimestamped exports must remain deterministic");
}

visualization::VisualChartPtr denseChart()
{
    auto document = basicDocument();
    document.tracks().push_back(document.tracks()[0]);
    document.tracks()[1].id = QStringLiteral("strings");
    for (int pitch = 36; pitch <= 84; ++pitch) {
        music::NoteEvent note;
        note.pitch = pitch;
        note.noteId = quint64(pitch);
        note.start = (pitch % 7) * 95;
        note.duration = pitch % 3 == 0 ? 2880 : 240;
        note.sustainEnd = note.start + 3600;
        note.velocity = 62 + pitch % 50;
        document.tracks()[pitch < 60 ? 0 : 1].notes.push_back(note);
    }
    document.rebuildMeasureGrid();
    return visualization::PlaybackVisualizationProjector().project(document, 1);
}

QImage renderRaster(const visualization::VisualChartPtr& chart, qint64 time, QSize size, qreal dpr = 1,
                    FallingNotesRenderer* reusedRenderer = nullptr)
{
    visualization::PlaybackSceneState state;
    state.chart = chart;
    state.transportPositionUs = time;
    state.transportState = playback::State::Playing;
    state.updateVisibleWindow();
    QVector<int> indices;
    visualization::VisibleNoteIndex(chart->notes()).query(state.visibleWindowStartUs, state.visibleWindowEndUs, indices);
    state.candidateNoteIndices = {indices.constData(), size_t(indices.size())};
    QImage image(QSize(qRound(size.width() * dpr), qRound(size.height() * dpr)), QImage::Format_ARGB32_Premultiplied);
    image.setDevicePixelRatio(dpr);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    FallingNotesRenderer renderer;
    auto& selected = reusedRenderer ? *reusedRenderer : renderer;
    selected.render(painter, SceneLayoutEngine().layout(size, chart.get(), state.lookAheadUs), state);
    return image;
}

void testFrameEffects(const visualization::VisualChartPtr& chart)
{
    const auto geometry = SceneLayoutEngine().layout({1280, 720}, chart.get(), 5'000'000);
    NoteRenderCache cache;
    cache.prepare(chart, geometry);
    visualization::PlaybackSceneState state;
    state.chart = chart;
    state.transportState = playback::State::Playing;
    state.transportPositionUs = 10'000;
    state.updateVisibleWindow();
    QVector<int> indices;
    visualization::VisibleNoteIndex(chart->notes()).query(state.visibleWindowStartUs, state.visibleWindowEndUs, indices);
    state.candidateNoteIndices = {indices.constData(), size_t(indices.size())};
    NoteFrameState frame;
    frame.prepare(state, geometry, cache);
    require(!frame.glows().isEmpty() && frame.glows().size() <= 96, "recent attacks need bounded local light effects");
    require(frame.bodyOpacity() < 1 && frame.bodyOpacity() >= .52, "density must reduce large-area visual weight");
    state.effectsStartUs = 10'000;
    frame.prepare(state, geometry, cache);
    for (int pitch = 0; pitch < 128; ++pitch)
        require(frame.key(pitch).attack == 0, "seek must not replay old attack effects");
    state.transportState = playback::State::Paused;
    frame.prepare(state, geometry, cache);
    require(frame.glows().isEmpty(), "paused scenes must not emit transient effects");
}

#if MIDI_PLAY_HAS_VULKAN
void captureVulkan(const visualization::VisualChartPtr& chart, const QString& directory)
{
    QVulkanInstance instance;
    require(instance.create(), "Vulkan instance must initialize");
    FallingNotesVulkanWindow window;
    window.setVulkanInstance(&instance);
    window.resize(1280, 720);
    window.setChart(chart);
    window.setTransportPosition(500'000, chart->durationUs());
    window.setTransportState(playback::State::Playing);
    bool failed = false;
    QObject::connect(&window, &FallingNotesVulkanWindow::initializationFailed, &window,
        [&](const QString& error) { std::fprintf(stderr, "%s\n", qPrintable(error)); failed = true; });
    window.show();
    QElapsedTimer timer;
    timer.start();
    while (!window.isValid() && !failed && timer.elapsed() < 5000) {
        QApplication::processEvents();
        QThread::msleep(2);
    }
    require(!failed && window.isValid() && window.supportsGrab(), "Vulkan must support render readback");
    const auto image = window.grab();
    require(!image.isNull(), "Vulkan must return a non-empty rendered frame");
    require(image.save(directory + QStringLiteral("/notes-vulkan.png")), "Vulkan snapshot must save");
    const auto reference = renderRaster(chart, 500'000, window.size(), window.devicePixelRatio());
    require(reference.size() == image.size(), "backend comparison must use equal physical dimensions");
    require(reference.save(directory + QStringLiteral("/notes-qt-matched.png")), "matched raster snapshot must save");
    double difference = 0;
    const int height = qRound(image.height() * 0.77);
    for (int y = 0; y < height; ++y) for (int x = 0; x < image.width(); ++x) {
        const auto a = image.pixelColor(x, y);
        const auto b = reference.pixelColor(x, y);
        difference += std::abs(a.red() - b.red()) + std::abs(a.green() - b.green()) + std::abs(a.blue() - b.blue());
    }
    difference /= height * image.width() * 3.0;
    std::printf("Qt/Vulkan falling-area mean channel difference: %.3f / 255\n", difference);
    require(difference < 4.0, "Qt and Vulkan must agree on note colors, geometry and layering");
    window.setTransportState(playback::State::Paused);
    window.hide();
}
#endif

void benchmarkRaster(int noteCount, bool pedalTails)
{
    auto document = basicDocument();
    for (int i = 0; i < noteCount; ++i) {
        music::NoteEvent note;
        note.pitch = 24 + i % 84;
        note.start = (i / 84) * 180;
        note.duration = 1800;
        if (pedalTails) note.sustainEnd = note.start + 3600;
        note.velocity = 48 + i % 80;
        document.tracks()[0].notes.push_back(note);
    }
    document.rebuildMeasureGrid();
    const auto chart = visualization::PlaybackVisualizationProjector().project(document, 1);
    visualization::PlaybackSceneState state;
    state.chart = chart;
    state.transportPositionUs = 500'000;
    state.transportState = playback::State::Playing;
    state.updateVisibleWindow();
    QVector<int> candidates;
    visualization::VisibleNoteIndex(chart->notes()).query(state.visibleWindowStartUs, state.visibleWindowEndUs, candidates);
    state.candidateNoteIndices = {candidates.constData(), size_t(candidates.size())};
    const auto geometry = SceneLayoutEngine().layout({1920, 1080}, chart.get(), state.lookAheadUs);
    QImage image(1920, 1080, QImage::Format_ARGB32_Premultiplied);
    FallingNotesRenderer renderer;
    QVector<double> samples;
    for (int frame = 0; frame < 65; ++frame) {
        state.transportPositionUs += 1000;
        QElapsedTimer timer;
        timer.start();
        QPainter painter(&image);
        renderer.render(painter, geometry, state);
        painter.end();
        if (frame >= 5) samples.push_back(timer.nsecsElapsed() / 1'000'000.0);
    }
    std::sort(samples.begin(), samples.end());
    std::printf("Qt 1920x1080, %lld candidates%s: p50 %.2f ms, p95 %.2f ms\n",
                qlonglong(candidates.size()), pedalTails ? " with pedal tails" : "", samples[30], samples[57]);
}

} // namespace

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    testPedalTimingSurvivesImport();
    testSostenutoCapturesOnlyHeldInstances();
    testContinuousBarsAndPolyphonicTies();
    testGeometryAndMaterials();
    testVisualClock();
    const auto chart = denseChart();
    testFrameEffects(chart);
    const auto first = renderRaster(chart, 500'000, {1280, 720});
    require(first == renderRaster(chart, 500'000, {1280, 720}), "raster snapshots must be deterministic");
    require(first != renderRaster(chart, 550'000, {1280, 720}), "notes must move continuously with song time");
    FallingNotesRenderer reusedRenderer;
    for (qreal dpr : {1.0, 1.5, 2.0}) {
        renderRaster(chart, 400'000, {960, 600}, dpr, &reusedRenderer);
        require(renderRaster(chart, 500'000, {1280, 720}, dpr, &reusedRenderer)
                == renderRaster(chart, 500'000, {1280, 720}, dpr),
                "cached renders must remain stable after resizing and DPI changes");
    }
    const auto args = app.arguments();
    if (args.contains(QStringLiteral("--benchmark"))) {
        for (int count : {160, 600, 1600}) benchmarkRaster(count, false);
        benchmarkRaster(600, true);
    }
    const int output = args.indexOf(QStringLiteral("--snapshots"));
    if (output >= 0 && output + 1 < args.size()) {
        const QString directory = args[output + 1];
        require(QDir().mkpath(directory), "snapshot directory must be writable");
        require(first.save(directory + QStringLiteral("/notes-qt.png")), "raster snapshot must save");
#if MIDI_PLAY_HAS_VULKAN
        if (args.contains(QStringLiteral("--vulkan"))) captureVulkan(chart, directory);
#endif
    }
    std::puts("Note timing, materials, geometry, animation and rendering checks passed");
}
