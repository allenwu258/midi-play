#include "domain/music/musicanalysis.h"
#include "domain/playback/playbackmodel.h"
#include "domain/visualization/playbackvisualizationprojector.h"
#include "domain/visualization/visiblenoteindex.h"
#include "infrastructure/midi/mididocumentbuilder.h"
#include "infrastructure/midi/midinormalizer.h"
#include "infrastructure/midi/midireader.h"
#include "presentation/visualization/fallingnotesrenderer.h"
#include "presentation/visualization/noteframestate.h"
#include "presentation/visualization/scenelayoutengine.h"
#include "presentation/visualization/visualplaybackclock.h"
#if MIDI_PLAY_HAS_VULKAN
#include "presentation/visualization/fallingnotesvulkanwindow.h"
#include "presentation/visualization/vulkanscene.h"
#include <QVulkanInstance>
#endif

#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QPainter>
#include <QThread>
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>

namespace {
using namespace midi_play;
using namespace midi_play::presentation::visualization;
using midi_play::settings::ThemeMode;
using midi_play::settings::NoteColorMode;

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
    music::MusicAnalyzer().analyze(document);
    return visualization::PlaybackVisualizationProjector().project(document, 1);
}

QImage renderRaster(const visualization::VisualChartPtr& chart, qint64 time, QSize size, qreal dpr = 1,
                    FallingNotesRenderer* reusedRenderer = nullptr, bool showNotationStrip = false,
                    ThemeMode mode = ThemeMode::Dark, NoteColorMode colors = NoteColorMode::Normal)
{
    visualization::PlaybackSceneState state;
    state.themeMode = mode;
    state.noteColorMode = colors;
    state.chart = chart;
    state.transportPositionUs = time;
    state.transportState = playback::State::Playing;
    state.showNotationStrip = showNotationStrip;
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
    selected.render(painter, SceneLayoutEngine().layout(size, chart.get(), state.lookAheadUs, showNotationStrip), state);
    return image;
}

void testNotationStripGeometry(const visualization::VisualChartPtr& chart)
{
    for (QSize size : {QSize(640, 440), QSize(1280, 720), QSize(2560, 1440)}) {
        for (bool show : {false, true}) {
            const auto geometry = SceneLayoutEngine().layout(size, chart.get(), 5'000'000, show);
            NoteRenderCache cache;
            cache.prepare(chart, geometry);
            const auto* note = cache.note(0);
            require(note != nullptr, "notation geometry test needs a note");
            const auto onset = noteGeometry(*note, geometry, note->startUs);
            require(qAbs(onset.head.bottom() - geometry.strikeLineY) < .001,
                    "the note attack must reach the strike line at its musical onset in both layouts");
            const auto future = noteGeometry(*note, geometry, note->startUs - 5'000'000);
            require(qAbs(future.head.bottom() - geometry.fallingRect.top()) < .001,
                    "layout changes must preserve the five-second look-ahead window");
        }
    }
    FallingNotesRenderer renderer;
    for (qreal dpr : {1.0, 1.5, 2.0}) {
        for (bool show : {false, true, false})
            require(renderRaster(chart, 500'000, {1280, 720}, dpr, &renderer, show)
                        == renderRaster(chart, 500'000, {1280, 720}, dpr, nullptr, show),
                    "toggling notation at the same size must match a fresh raster renderer");
    }
}

void testThemeMaterialsAndRaster(const visualization::VisualChartPtr& chart)
{
    const auto geometry = SceneLayoutEngine().layout({1280, 720}, chart.get(), 5'000'000);
    NoteRenderCache cache;
    cache.prepare(chart, geometry);
    const auto darkBody = cache.styleForNote(0)->material.body;
    const auto chartBuilds = cache.chartBuildCount();
    const auto geometryBuilds = cache.geometryBuildCount();
    const auto materialRevision = cache.materialRevision();
    cache.prepare(chart, geometry, ThemeMode::Light);
    require(cache.styleForNote(0)->material.body != darkBody && cache.materialRevision() > materialRevision,
            "light theme must rebuild note materials");
    require(cache.chartBuildCount() == chartBuilds && cache.geometryBuildCount() == geometryBuilds,
            "theme changes must not rebuild chart data or note geometry");
    const auto lightRevision = cache.materialRevision();
    cache.prepare(chart, geometry, ThemeMode::Light);
    require(cache.materialRevision() == lightRevision, "unchanged themes must preserve material caches");
    cache.prepare(chart, geometry, ThemeMode::Dark);
    require(cache.styleForNote(0)->material.body == darkBody, "returning to dark must restore original materials");

    FallingNotesRenderer renderer;
    for (qreal dpr : {1.0, 1.5, 2.0}) {
        for (bool show : {false, true}) {
            QImage previous;
            for (auto mode : {ThemeMode::Dark, ThemeMode::Light, ThemeMode::Dark}) {
                const auto cached = renderRaster(chart, 500'000, {1280, 720}, dpr, &renderer, show, mode);
                require(cached == renderRaster(chart, 500'000, {1280, 720}, dpr, nullptr, show, mode),
                        "theme toggles must invalidate colored raster caches at every DPI and notation layout");
                require(previous.isNull() || cached != previous, "both palettes must produce distinct scenes");
                previous = cached;
            }
        }
    }
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
visualization::VisualChartPtr stressChart()
{
    auto document = basicDocument();
    for (int i = 0; i < 4096; ++i) {
        music::NoteEvent note;
        note.noteId = quint64(i + 1);
        note.pitch = 24 + i % 84;
        note.start = (i / 84) * 40;
        note.duration = 960 + i % 480;
        note.sustainEnd = note.start + 2400;
        note.velocity = 48 + i % 80;
        document.tracks()[0].notes.push_back(note);
    }
    document.rebuildMeasureGrid();
    return visualization::PlaybackVisualizationProjector().project(document, 3);
}

void testVulkanStaticKeyboard(const visualization::VisualChartPtr& chart)
{
    VulkanScene scene;
    visualization::PlaybackSceneState state;
    state.chart = chart;
    state.transportState = playback::State::Playing;
    scene.prepare(state, {1280, 720}, 1, QFont());
    const auto revision = scene.staticUiRevision();
    const auto keyboard = scene.staticUi().quads;
    for (qint64 time : {10'000, 500'000, 2'000'000, 500'000}) {
        state.transportPositionUs = time;
        scene.prepare(state, {1280, 720}, 1, QFont());
        require(scene.staticUiRevision() == revision && scene.staticUi().quads == keyboard,
                "animation and seeking must not rebuild the static keyboard");
    }
    // Static vertices use logical coordinates and contain no atlas UVs.
    scene.prepare(state, {1280, 720}, 2, QFont(QStringLiteral("Arial"), 12));
    require(scene.staticUiRevision() == revision && scene.staticUi().quads == keyboard,
            "DPI and font changes must not invalidate atlas-independent geometry");
    scene.prepare(state, {960, 640}, 2, QFont());
    require(scene.staticUiRevision() != revision && scene.staticUi().quads != keyboard,
            "resizing must rebuild the static keyboard");
    for (bool show : {false, true, false}) {
        state.showNotationStrip = show;
        scene.prepare(state, {960, 640}, 2, QFont());
        const auto& geometry = scene.geometry();
        require(geometry.notationStripRect.isEmpty() != show,
                "Vulkan must update notation layout even when size and chart are unchanged");
        const auto range = scene.dynamicUi().range(VulkanUiLayer::Strike);
        int labels = 0;
        for (uint32_t i = range.first; i < range.first + range.count; ++i)
            if (scene.dynamicUi().quads[i].options[2] > 0) ++labels;
        require(show ? labels > 0 : labels == 0,
                "notation visibility must control strike-label glyphs");
    }
}

void testVulkanThemes(const visualization::VisualChartPtr& chart)
{
    VulkanScene scene;
    visualization::PlaybackSceneState state;
    state.chart = chart;
    state.transportState = playback::State::Playing;
    state.transportPositionUs = 500'000;
    for (bool show : {false, true}) {
        state.showNotationStrip = show;
        state.themeMode = ThemeMode::Dark;
        scene.prepare(state, {1280, 720}, 1.5, QFont());
        const auto darkKeys = scene.staticUi().quads;
        const auto darkNotes = scene.notes();
        const auto darkDecorations = scene.dynamicUi().quads;
        const auto atlasRevision = scene.atlasRevision();
        const auto strikeY = scene.geometry().strikeLineY;
        const auto staticRevision = scene.staticUiRevision();
        const auto notesRevision = scene.notesRevision();
        state.themeMode = ThemeMode::Light;
        scene.prepare(state, {1280, 720}, 1.5, QFont());
        require(scene.staticUi().quads != darkKeys && scene.notes() != darkNotes
                    && scene.dynamicUi().quads != darkDecorations,
                "Vulkan theme changes must recolor static, note and dynamic batches together");
        require(scene.staticUiRevision() > staticRevision && scene.notesRevision() > notesRevision,
                "all colored GPU buffers must be marked for upload");
        require(scene.atlasRevision() == atlasRevision && scene.geometry().strikeLineY == strikeY,
                "theme-only updates must retain glyph atlas and strike geometry");
        const auto lightNotesRevision = scene.notesRevision();
        const auto lightStaticRevision = scene.staticUiRevision();
        scene.prepare(state, {1280, 720}, 1.5, QFont());
        require(scene.notesRevision() == lightNotesRevision && scene.staticUiRevision() == lightStaticRevision,
                "unchanged themes must not upload static GPU data again");
        state.themeMode = ThemeMode::Dark;
        scene.prepare(state, {1280, 720}, 1.5, QFont());
        require(scene.staticUi().quads == darkKeys && scene.notes() == darkNotes
                    && scene.dynamicUi().quads == darkDecorations,
                "Vulkan must restore every batch deterministically after a theme round trip");
        if (!show) {
            const auto range = scene.dynamicUi().range(VulkanUiLayer::Strike);
            for (uint32_t i = range.first; i < range.first + range.count; ++i)
                require(scene.dynamicUi().quads[i].options[3] == 2,
                        "hidden notation may retain local note effects, but no strike line or yellow glow");
        }
    }
}

void testVulkanKeyboardFrames(const visualization::VisualChartPtr& chart, const QString& directory)
{
    qputenv("MIDI_PLAY_VULKAN_VALIDATE_RESOURCES", "1");
    QVulkanInstance instance;
    std::atomic<int> validationErrors = 0;
    if (instance.supportedLayers().contains("VK_LAYER_KHRONOS_validation"))
        instance.setLayers({"VK_LAYER_KHRONOS_validation"});
    require(instance.create(), "Vulkan instance must initialize");
    instance.installDebugOutputFilter([&](QVulkanInstance::DebugMessageSeverityFlags severity,
        QVulkanInstance::DebugMessageTypeFlags, const void* message) {
        if (severity & QVulkanInstance::ErrorSeverity) {
            ++validationErrors;
            const auto* data = static_cast<const VkDebugUtilsMessengerCallbackDataEXT*>(message);
            std::fprintf(stderr, "Vulkan validation: %s\n", data->pMessage);
        }
        return false;
    });
    FallingNotesVulkanWindow window;
    window.setVulkanInstance(&instance);
    window.resize(1280, 720);
    window.setChart(chart);
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
    require(!failed && window.isValid() && window.supportsGrab(), "Vulkan must support keyboard readback");
    std::fprintf(stderr, "GPU: %s; frames=%d; images=%d\n", window.physicalDeviceProperties()->deviceName,
        window.concurrentFrameCount(), window.swapChainImageCount());
    int presentedFrames = 0;
    QObject::connect(&window, &FallingNotesVulkanWindow::frameRendered, &window, [&] { ++presentedFrames; });
    window.resize(2560, 1440);
    const auto advance = QObject::connect(&window, &FallingNotesVulkanWindow::frameRendered, &window, [&] {
        window.setTransportPosition((qint64(presentedFrames) * 250'000) % chart->durationUs(), chart->durationUs());
        window.setShowNotationStrip((presentedFrames / 60) % 2 != 0);
        window.setThemeMode((presentedFrames / 90) % 2 ? ThemeMode::Light : ThemeMode::Dark);
        window.setNoteColorMode((presentedFrames / 45) % 2 ? NoteColorMode::Vivid : NoteColorMode::Normal);
    });
    while (presentedFrames < 900 && !failed && timer.elapsed() < 30'000) {
        QApplication::processEvents();
        QThread::msleep(1);
    }
    require(!failed && presentedFrames >= 900, "continuous playback must not reuse in-flight GPU resources");
    std::fprintf(stderr, "Continuous playback complete: validation errors=%d\n", int(validationErrors));
    QObject::disconnect(advance);
    window.setShowNotationStrip(false);
    window.resize(1280, 720);
    NoteRenderCache cache;
    NoteFrameState noteFrame;
    const auto geometry = SceneLayoutEngine().layout(window.size(), chart.get(), 5'000'000);
    cache.prepare(chart, geometry);
    visualization::VisibleNoteIndex noteIndex(chart->notes());
    QVector<int> indices;
    int frameCount = 0;
    for (qint64 time = 0; time < chart->durationUs(); time += 250'000) {
        const auto mode = frameCount % 2 ? ThemeMode::Light : ThemeMode::Dark;
        const auto colors = (frameCount / 2) % 2 ? NoteColorMode::Vivid : NoteColorMode::Normal;
        const auto& theme = presentation::theme::themeFor(mode).visualization;
        cache.prepare(chart, geometry, mode, colors);
        window.setThemeMode(mode);
        window.setNoteColorMode(colors);
        window.setTransportPosition(time, chart->durationUs());
        const int targetFrame = presentedFrames + 3;
        QElapsedTimer frameTimer;
        frameTimer.start();
        while (presentedFrames < targetFrame && !failed && frameTimer.elapsed() < 3000) {
            QApplication::processEvents();
            QThread::msleep(1);
        }
        require(!failed && presentedFrames >= targetFrame, "continuous Vulkan frames must advance");
        const auto image = window.grab();
        require(!image.isNull(), "keyboard frame must render");
        auto state = window.sceneState();
        noteIndex.query(state.visibleWindowStartUs, state.visibleWindowEndUs, indices);
        state.candidateNoteIndices = {indices.constData(), size_t(indices.size())};
        noteFrame.prepare(state, geometry, cache);
        // Interior samples avoid borders, labels and black/white key overlap.
        for (const auto& slot : geometry.pitches) {
            if (!slot.valid) continue;
            const QPointF sample(slot.keyRect.center().x(), slot.keyRect.top() + slot.keyRect.height() * .8);
            const auto& light = noteFrame.key(slot.pitch);
            const auto* style = cache.styleForNote(light.noteIndex);
            const auto base = slot.blackKey ? theme.blackKey : theme.whiteKey;
            const QColor expected = style ? illuminatedKeyColor(base, style->material.keyFill,
                light.strength * (slot.blackKey ? .70 : .60)) : base;
            const auto actual = image.pixelColor(qRound(sample.x() * window.devicePixelRatio()),
                                                 qRound(sample.y() * window.devicePixelRatio()));
            const int error = std::max({std::abs(expected.red() - actual.red()),
                std::abs(expected.green() - actual.green()), std::abs(expected.blue() - actual.blue())});
            if (error > 4) {
                QDir().mkpath(directory);
                image.save(directory + QStringLiteral("/keyboard-failure.png"));
                std::fprintf(stderr, "Keyboard mismatch: time=%lld pitch=%d expected=%s actual=%s error=%d\n",
                    time, slot.pitch, qPrintable(expected.name()), qPrintable(actual.name()), error);
                require(false, "Vulkan keyboard must match the opaque scene fill in every frame");
            }
        }
        ++frameCount;
    }
    std::printf("Vulkan keyboard stability: %d sampled frames, %d submitted frames, %lld notes, %.2f seconds\n",
                frameCount, presentedFrames, qlonglong(chart->notes().size()), timer.elapsed() / 1000.0);
    window.setTransportState(playback::State::Paused);
    window.hide();
    window.destroy();
    instance.destroy();
    require(validationErrors == 0, "Vulkan lifetime and synchronization validation must pass");
}

void captureVulkan(const visualization::VisualChartPtr& chart, const QString& directory, bool showNotationStrip,
                   ThemeMode mode, NoteColorMode colors)
{
    QVulkanInstance instance;
    require(instance.create(), "Vulkan instance must initialize");
    FallingNotesVulkanWindow window;
    window.setVulkanInstance(&instance);
    window.resize(1280, 720);
    window.setChart(chart);
    window.setShowNotationStrip(showNotationStrip);
    window.setThemeMode(mode);
    window.setNoteColorMode(colors);
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
    const auto reference = renderRaster(chart, 500'000, window.size(), window.devicePixelRatio(), nullptr, showNotationStrip, mode, colors);
    require(reference.size() == image.size(), "backend comparison must use equal physical dimensions");
    require(reference.save(directory + QStringLiteral("/notes-qt-matched.png")), "matched raster snapshot must save");
    double difference = 0;
    const int height = qFloor(SceneLayoutEngine().layout(window.size(), chart.get(), 5'000'000, showNotationStrip)
        .fallingRect.bottom() * window.devicePixelRatio());
    for (int y = 0; y < height; ++y) for (int x = 0; x < image.width(); ++x) {
        const auto a = image.pixelColor(x, y);
        const auto b = reference.pixelColor(x, y);
        difference += std::abs(a.red() - b.red()) + std::abs(a.green() - b.green()) + std::abs(a.blue() - b.blue());
    }
    difference /= height * image.width() * 3.0;
    std::printf("Qt/Vulkan falling-area mean channel difference: %.3f / 255\n", difference);
    require(difference < 4.0, "Qt and Vulkan must agree on note colors, geometry and layering");
    const auto geometry = SceneLayoutEngine().layout(window.size(), chart.get(), 5'000'000);
    double keyboardDifference = 0;
    int keyboardPixels = 0;
    for (const auto& slot : geometry.pitches) {
        if (!slot.valid) continue;
        // QPainter centers its pen; Vulkan uses an inset analytic border.
        // Compare filled interiors, excluding that known rasterization and
        // font difference. Black-key samples still verify white-key occlusion.
        QRectF interior = slot.keyRect.adjusted(3, 8, -3, -20);
        if (!slot.blackKey)
            interior.setTop(geometry.keyboardRect.top() + geometry.keyboardRect.height() * .62 + 3);
        const qreal dpr = window.devicePixelRatio();
        for (int y = qCeil(interior.top() * dpr); y < qFloor(interior.bottom() * dpr); ++y)
            for (int x = qCeil(interior.left() * dpr); x < qFloor(interior.right() * dpr); ++x) {
                const auto a = image.pixelColor(x, y);
                const auto b = reference.pixelColor(x, y);
                keyboardDifference += std::abs(a.red() - b.red()) + std::abs(a.green() - b.green()) + std::abs(a.blue() - b.blue());
                ++keyboardPixels;
            }
    }
    require(keyboardPixels > 0, "keyboard comparison must sample visible key interiors");
    keyboardDifference /= keyboardPixels * 3.0;
    std::printf("Qt/Vulkan key-interior mean channel difference: %.3f / 255\n", keyboardDifference);
    require(keyboardDifference < 2.0, "split keyboard batches must preserve key colors and black-key occlusion");
    window.setTransportState(playback::State::Paused);
    window.hide();
    window.destroy();
}
#endif

void benchmarkRaster(int noteCount, bool pedalTails, NoteColorMode colors)
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
    state.noteColorMode = colors;
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
    std::printf("Qt 1920x1080 %s, %lld candidates%s: p50 %.2f ms, p95 %.2f ms, p99 %.2f ms\n",
                colors == NoteColorMode::Vivid ? "vivid" : "normal", qlonglong(candidates.size()),
                pedalTails ? " with pedal tails" : "", samples[30], samples[57], samples.back());
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
    testNotationStripGeometry(chart);
    testThemeMaterialsAndRaster(chart);
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
#if MIDI_PLAY_HAS_VULKAN
    testVulkanStaticKeyboard(chart);
    testVulkanThemes(chart);
    if (args.contains(QStringLiteral("--vulkan-stress"))) {
        auto selectedChart = stressChart();
        const int midiArgument = args.indexOf(QStringLiteral("--midi"));
        if (midiArgument >= 0 && midiArgument + 1 < args.size()) {
            const auto result = midi::MidiReader().read(args[midiArgument + 1]);
            require(result.ok(), "stress MIDI must load");
            selectedChart = visualization::PlaybackVisualizationProjector().project(*result.document, 2);
        }
        testVulkanKeyboardFrames(selectedChart, QStringLiteral("build/vulkan-diagnostics"));
    }
#endif
    if (args.contains(QStringLiteral("--benchmark"))) {
        for (auto colors : {NoteColorMode::Normal, NoteColorMode::Vivid}) {
            for (int count : {160, 600, 1600}) benchmarkRaster(count, false, colors);
            benchmarkRaster(600, true, colors);
        }
    }
    const int output = args.indexOf(QStringLiteral("--snapshots"));
    if (output >= 0 && output + 1 < args.size()) {
        const QString directory = args[output + 1];
        require(QDir().mkpath(directory), "snapshot directory must be writable");
        require(first.save(directory + QStringLiteral("/notes-qt.png")), "raster snapshot must save");
#if MIDI_PLAY_HAS_VULKAN
        if (args.contains(QStringLiteral("--vulkan"))) {
            for (auto mode : {ThemeMode::Dark, ThemeMode::Light}) {
                for (auto colors : {NoteColorMode::Normal, NoteColorMode::Vivid}) for (bool show : {false, true}) {
                    const auto variant = directory + (mode == ThemeMode::Dark ? QStringLiteral("/dark") : QStringLiteral("/light"))
                        + (colors == NoteColorMode::Normal ? QStringLiteral("/normal") : QStringLiteral("/vivid"))
                        + (show ? QStringLiteral("/notation-shown") : QStringLiteral("/notation-hidden"));
                    require(QDir().mkpath(variant), "theme snapshot directory must be writable");
                    captureVulkan(chart, variant, show, mode, colors);
                }
            }
        }
#endif
    }
    std::puts("Note timing, materials, geometry, animation and rendering checks passed");
}
