#include "app/playerapplicationservice.h"
#include "app/settingsservice.h"
#include "domain/visualization/playbackvisualizationprojector.h"
#include "domain/visualization/visiblenoteindex.h"
#include "infrastructure/settings/qsettingsstore.h"
#include "presentation/mainwindow.h"
#include "presentation/settings/settingsdialog.h"
#include "presentation/visualization/fallingnotesview.h"
#include "presentation/visualization/notecolorpalette.h"
#if MIDI_PLAY_HAS_VULKAN
#include "presentation/visualization/fallingnotesvulkanwindow.h"
#include "presentation/visualization/vulkanscene.h"
#endif

#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QLabel>
#include <QPainter>
#include <QSettings>
#include <QTemporaryDir>
#include <QThread>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace {
using namespace midi_play;
using namespace midi_play::presentation::visualization;
using app::SettingsService;
using infrastructure::settings::QSettingsStore;
using settings::NoteColorMode;
using settings::ThemeMode;

void require(bool value, const char* message)
{
    if (!value) { std::fprintf(stderr, "FAILED: %s\n", message); std::exit(EXIT_FAILURE); }
}

visualization::VisualChartPtr sampleChart(int count = 132, bool varied = true)
{
    music::MusicDocument document;
    document.tempos().push_back({0, 120.0, 0});
    document.setDuration(std::max<qint64>(5760, qint64(count) * 60));
    document.tracks().resize(11);
    for (int t = 0; t < 11; ++t) {
        auto& track = document.tracks()[t];
        track.id = QString::number(t);
        track.percussion = t == 10;
        track.channel = t == 10 ? 9 : 0;
    }
    for (int i = 0; i < count; ++i) {
        music::NoteEvent note;
        note.pitch = varied ? 36 + i % 60 : 60;
        note.noteId = quint64(i + 1);
        note.start = (i / 12) * 60;
        note.duration = 480;
        note.sustainEnd = note.start + 1920;
        note.velocity = varied ? 24 + (i * 7) % 104 : 88;
        note.staff = varied ? 1 + (i / 11) % 2 : 1;
        note.voice = varied ? 1 + (i / 23) % 3 : 1;
        note.ghost = varied && i % 29 == 0;
        note.tremolo = varied && i % 31 == 0;
        document.tracks()[varied ? i % 11 : 0].notes.push_back(note);
    }
    document.rebuildMeasureGrid();
    return visualization::PlaybackVisualizationProjector().project(document, 1);
}

void testPersistence()
{
    QTemporaryDir directory;
    require(directory.isValid(), "temporary settings directory must exist");
    const auto path = directory.filePath(QStringLiteral("settings.ini"));
    QSettingsStore store(path);
    QString warning;
    require(store.load(&warning).noteColorMode == NoteColorMode::Vivid && warning.isEmpty(),
            "new users must get vivid without a warning or a settings write");
    require(!QFileInfo::exists(path), "reading a default must not write a configuration");
    {
        QSettings legacy(path, QSettings::IniFormat);
        legacy.setValue(QStringLiteral("General/schemaVersion"), 6);
        legacy.setValue(QStringLiteral("General/themeMode"), 1);
        legacy.setValue(QStringLiteral("General/visualizationRefreshRate"), 120);
        legacy.setValue(QStringLiteral("General/showNotationStrip"), true);
        legacy.setValue(QStringLiteral("Audio/soundFontPath"), directory.filePath(QStringLiteral("piano.sf2")));
    }
    SettingsService service(std::make_unique<QSettingsStore>(path));
    service.load();
    const auto soundFont = service.soundFontPath();
    require(service.noteColorMode() == NoteColorMode::Vivid && service.themeMode() == ThemeMode::Light
                && service.visualizationRefreshRate() == 120 && service.showNotationStrip(),
            "schema 6 must gain the vivid default without resetting other preferences");
    int changes = 0;
    QObject::connect(&service, &SettingsService::noteColorModeChanged, &service, [&](auto) { ++changes; });
    service.setNoteColorMode(NoteColorMode::Normal);
    service.setNoteColorMode(NoteColorMode::Normal);
    service.setThemeMode(ThemeMode::Dark);
    SettingsService restarted(std::make_unique<QSettingsStore>(path));
    restarted.load();
    require(changes == 1 && restarted.noteColorMode() == NoteColorMode::Normal
                && restarted.themeMode() == ThemeMode::Dark && restarted.showNotationStrip()
                && restarted.visualizationRefreshRate() == 120 && restarted.soundFontPath() == soundFont,
            "normal must persist independently of theme and unrelated settings");
    require(QSettings(path, QSettings::IniFormat).value(QStringLiteral("General/schemaVersion")).toInt() == 7,
            "saving note colors must persist schema 7");
    for (const QVariant& invalid : {QVariant(-1), QVariant(9), QVariant(QStringLiteral("vivid")), QVariant(QString())}) {
        {
            QSettings file(path, QSettings::IniFormat);
            file.setValue(QStringLiteral("General/noteColorMode"), invalid);
        }
        restarted.load();
        require(restarted.noteColorMode() == NoteColorMode::Vivid && !restarted.lastLoadWarning().isEmpty()
                    && restarted.visualizationRefreshRate() == 120,
                "malformed and out-of-range colors must warn and fall back to vivid, never integer zero");
    }
    restarted.setNoteColorMode(NoteColorMode::Normal);
    require(restarted.lastLoadWarning().isEmpty(), "successful repair must clear the active load warning");
    presentation::settings::SettingsDialog repaired(&restarted, nullptr);
    require(repaired.findChild<QLabel*>(QStringLiteral("settingsError"))->isHidden(),
            "a new dialog must not repeat a repaired warning");
    service.setNoteColorMode(static_cast<NoteColorMode>(-1));
    require(service.noteColorMode() == NoteColorMode::Vivid && changes == 2,
            "invalid programmatic values must normalize to vivid exactly once");
}

class ControlledStore final : public app::ISettingsStore {
public:
    bool fail = true;
    int writes = 0;
    settings::PlayerSettings saved;
    settings::PlayerSettings load(QString* warning) override
    {
        if (warning) *warning = QStringLiteral("Invalid note color preference");
        return saved;
    }
    bool save(const settings::PlayerSettings& value, QString*) override
    {
        ++writes;
        if (fail) return false; // The service must still provide an error message.
        saved = value;
        return true;
    }
};

void testSaveFailureAndControls()
{
    auto store = std::make_unique<ControlledStore>();
    auto* persistence = store.get();
    SettingsService service(std::move(store));
    service.load();
    app::PlayerApplicationService player;
    presentation::MainWindow window(&player, &service);
    presentation::settings::SettingsDialog dialog(&service, nullptr);
    auto* combo = dialog.findChild<QComboBox*>(QStringLiteral("noteColorModeCombo"));
    auto* error = dialog.findChild<QLabel*>(QStringLiteral("settingsError"));
    auto* view = window.findChild<FallingNotesView*>();
    require(combo && view && combo->count() == 2 && combo->currentData().toInt() == 1,
            "settings and view must initialize to vivid without relying on combo order");
    require(persistence->writes == 0, "control initialization must not save defaults");
    combo->setCurrentIndex(combo->findData(0));
    require(service.noteColorMode() == NoteColorMode::Normal && view->noteColorMode() == NoteColorMode::Normal
                && persistence->saved.noteColorMode == NoteColorMode::Vivid
                && !error->isHidden() && !error->text().isEmpty() && !service.lastLoadWarning().isEmpty(),
            "failed saves must keep the selected live mode and warning, without claiming persistence");
    service.setNoteColorMode(NoteColorMode::Normal);
    require(persistence->writes == 1, "unchanged selections must not save repeatedly");
    persistence->fail = false;
    combo->setCurrentIndex(combo->findData(1));
    require(persistence->saved.noteColorMode == NoteColorMode::Vivid && service.lastLoadWarning().isEmpty(),
            "a later successful save must repair the configuration");
    service.setNoteColorMode(NoteColorMode::Normal);
    presentation::settings::SettingsDialog reopened(&service, nullptr);
    require(reopened.findChild<QComboBox*>(QStringLiteral("noteColorModeCombo"))->currentData().toInt() == 0,
            "new settings windows must inherit the chosen mode");
}

void testMaterials(const visualization::VisualChartPtr& chart)
{
    visualization::VisualNote a;
    a.trackIndex = 0;
    a.pitch = 60;
    auto b = a;
    b.pitch = 64; // These shared the old pitch/6 bucket.
    require(noteMaterialKey(a) != noteMaterialKey(b), "different pitch classes must never alias in the style cache");
    const auto& normal = noteAppearanceFor(ThemeMode::Dark, NoteColorMode::Normal);
    const auto& vivid = noteAppearanceFor(ThemeMode::Dark, NoteColorMode::Vivid);
    require(makeNoteMaterial(*chart, a, normal).body == makeNoteMaterial(*chart, b, normal).body,
            "normal must retain the old six-semitone color grouping");
    require(makeNoteMaterial(*chart, a, vivid).body != makeNoteMaterial(*chart, b, vivid).body,
            "vivid must distinguish pitches inside the old bucket");
    b = a;
    b.instanceId = 91;
    b.sourceNoteId = 55;
    b.repeatPass = 3;
    b.startUs = 1'000'000;
    require(noteMaterialKey(a) == noteMaterialKey(b)
                && makeNoteMaterial(*chart, a, vivid).body == makeNoteMaterial(*chart, b, vivid).body,
            "order, timing and repeat identity must not alter colors");
    b.flags |= visualization::PercussionNote;
    require(noteMaterialKey(a) != noteMaterialKey(b), "percussion must not alias melodic styles");
    require(vividNoteHue(0, 35, true) == vividNoteHue(0, 36, true)
                && vividNoteHue(0, 36, true) != vividNoteHue(0, 42, true),
            "drum colors must follow instrument families instead of pitch class");
    for (auto theme : {ThemeMode::Dark, ThemeMode::Light}) {
        for (auto colors : {NoteColorMode::Normal, NoteColorMode::Vivid}) {
            for (int track = -1; track < 12; ++track) for (int pitch = 0; pitch < 128; ++pitch) {
                for (int velocity : {1, 64, 127}) {
                    auto note = a;
                    note.trackIndex = track;
                    note.pitch = pitch;
                    note.velocity = velocity;
                    note.flags = (pitch % 3 ? 0U : visualization::GhostNote)
                        | (track == 10 ? visualization::PercussionNote : 0U);
                    const auto material = makeNoteMaterial(*chart, note, noteAppearanceFor(theme, colors));
                    for (const auto& color : {material.body, material.head, material.tail,
                                             material.keyFill, material.keyTop, material.glow}) {
                        require(color.isValid(), "all supported note colors must be valid");
                        for (float value : {color.redF(), color.greenF(), color.blueF(), color.alphaF()})
                            require(std::isfinite(value) && value >= 0 && value <= 1, "material must remain in sRGB gamut");
                    }
                    require(material.head.alphaF() > material.body.alphaF()
                                && material.tail.alphaF() < material.body.alphaF(), "material hierarchy must survive every hue");
                }
            }
        }
    }
}

QImage render(const visualization::VisualChartPtr& chart, FallingNotesRenderer& renderer,
              ThemeMode theme, NoteColorMode colors, bool notation, qreal dpr)
{
    visualization::PlaybackSceneState state;
    state.chart = chart;
    state.themeMode = theme;
    state.noteColorMode = colors;
    state.showNotationStrip = notation;
    state.transportState = playback::State::Playing;
    state.transportPositionUs = 500'000;
    state.updateVisibleWindow();
    QVector<int> indices;
    visualization::VisibleNoteIndex(chart->notes()).query(state.visibleWindowStartUs, state.visibleWindowEndUs, indices);
    state.candidateNoteIndices = {indices.constData(), size_t(indices.size())};
    const QSize size(960, 640);
    QImage image(QSize(qRound(size.width() * dpr), qRound(size.height() * dpr)), QImage::Format_ARGB32_Premultiplied);
    image.setDevicePixelRatio(dpr);
    QPainter painter(&image);
    renderer.render(painter, SceneLayoutEngine().layout(size, chart.get(), state.lookAheadUs, notation), state);
    return image;
}

void testCachesAndRaster(const visualization::VisualChartPtr& chart, const QString& snapshots)
{
    const auto geometry = SceneLayoutEngine().layout({960, 640}, chart.get(), 5'000'000);
    NoteRenderCache cache;
    cache.prepare(chart, geometry, ThemeMode::Dark, NoteColorMode::Normal);
    const auto originalNotes = cache.notes();
    const auto chartBuilds = cache.chartBuildCount(), geometryBuilds = cache.geometryBuildCount();
    auto revision = cache.materialRevision();
    for (auto theme : {ThemeMode::Dark, ThemeMode::Light}) {
        for (auto colors : {NoteColorMode::Vivid, NoteColorMode::Normal}) {
            cache.prepare(chart, geometry, theme, colors);
            require(cache.materialRevision() > revision && cache.chartBuildCount() == chartBuilds
                        && cache.geometryBuildCount() == geometryBuilds, "mode switches must rebuild materials only");
            revision = cache.materialRevision();
            cache.prepare(chart, geometry, theme, colors);
            require(cache.materialRevision() == revision, "same appearance must not invalidate caches");
            for (int i = 0; i < originalNotes.size(); ++i) {
                const auto& before = originalNotes[i];
                const auto* after = cache.note(i);
                const auto expected = makeNoteMaterial(*chart, chart->notes()[i], noteAppearanceFor(theme, colors));
                require(cache.styleForNote(i)->material.body == expected.body
                            && cache.styleForNote(i)->material.head == expected.head,
                        "every cached note must retain its own pitch, velocity, ghost and percussion color");
                require(before.styleIndex == after->styleIndex && before.left == after->left
                            && before.width == after->width && before.startUs == after->startUs
                            && before.keyEndUs == after->keyEndUs && before.audibleEndUs == after->audibleEndUs,
                        "recoloring must preserve styles, geometry and note timing");
            }
        }
        for (bool notation : {false, true}) for (qreal dpr : {1.0, 1.5, 2.0}) {
            FallingNotesRenderer reused;
            const auto normal = render(chart, reused, theme, NoteColorMode::Normal, notation, dpr);
            const auto vivid = render(chart, reused, theme, NoteColorMode::Vivid, notation, dpr);
            FallingNotesRenderer fresh;
            require(vivid != normal && vivid == render(chart, fresh, theme, NoteColorMode::Vivid, notation, dpr),
                    "a reused renderer must match fresh vivid rendering at every DPI and layout");
            require(normal == render(chart, reused, theme, NoteColorMode::Normal, notation, dpr),
                    "colored raster textures must restore exactly on mode round trips");
            if (!snapshots.isEmpty() && dpr == 1) {
                const auto name = QStringLiteral("/%1-%2").arg(theme == ThemeMode::Light ? "light" : "dark")
                    .arg(notation ? "shown" : "hidden");
                require(normal.save(snapshots + name + QStringLiteral("-normal.png"))
                            && vivid.save(snapshots + name + QStringLiteral("-vivid.png")), "color snapshots must save");
            }
        }
    }
}

#if MIDI_PLAY_HAS_VULKAN
void testVulkanRevisions(const visualization::VisualChartPtr& chart)
{
    for (auto theme : {ThemeMode::Dark, ThemeMode::Light}) for (bool notation : {false, true}) {
        visualization::PlaybackSceneState state;
        state.chart = chart;
        state.themeMode = theme;
        state.noteColorMode = NoteColorMode::Normal;
        state.transportState = playback::State::Playing;
        state.transportPositionUs = 500'000;
        state.showNotationStrip = notation;
        VulkanScene scene;
        scene.prepare(state, {960, 640}, 1.5, QFont());
        const auto keys = scene.staticUi().quads, notes = scene.notes(), dynamic = scene.dynamicUi().quads;
        const auto keyRevision = scene.staticUiRevision(), atlas = scene.atlasRevision(), revision = scene.notesRevision();
        const auto strike = scene.geometry().strikeLineY;
        state.noteColorMode = NoteColorMode::Vivid;
        scene.prepare(state, {960, 640}, 1.5, QFont());
        require(scene.notes() != notes && scene.dynamicUi().quads != dynamic && scene.notesRevision() > revision,
                "GPU color switches must update notes and active key effects");
        require(scene.staticUi().quads == keys && scene.staticUiRevision() == keyRevision
                    && scene.atlasRevision() == atlas && scene.geometry().strikeLineY == strike,
                "color-only switches must preserve static keys, atlas and strike geometry");
        const auto vividRevision = scene.notesRevision();
        scene.prepare(state, {960, 640}, 1.5, QFont());
        require(scene.notesRevision() == vividRevision, "steady appearance must not upload notes repeatedly");
        state.noteColorMode = NoteColorMode::Normal;
        scene.prepare(state, {960, 640}, 1.5, QFont());
        require(scene.notes() == notes && scene.dynamicUi().quads == dynamic, "Vulkan material round trips must be exact");
    }
}
#endif

template<class Predicate> bool waitUntil(Predicate predicate, int timeoutMs = 5000)
{
    QElapsedTimer timer;
    timer.start();
    while (!predicate() && timer.elapsed() < timeoutMs) {
        QApplication::processEvents();
        QThread::msleep(2);
    }
    return predicate();
}

void benchmarkStyles(const visualization::VisualChartPtr& chart, const char* description)
{
    const auto geometry = SceneLayoutEngine().layout({1920, 1080}, chart.get(), 5'000'000);
    NoteRenderCache cache;
    QElapsedTimer timer;
    timer.start();
    cache.prepare(chart, geometry, ThemeMode::Dark, NoteColorMode::Normal);
    const double coldMs = timer.nsecsElapsed() / 1'000'000.0;
    QVector<double> timings;
    for (int cycle = 0; cycle < 24; ++cycle) {
        timer.restart();
        cache.prepare(chart, geometry, ThemeMode::Dark, cycle % 2 ? NoteColorMode::Normal : NoteColorMode::Vivid);
        timings.push_back(timer.nsecsElapsed() / 1'000'000.0);
    }
    std::sort(timings.begin(), timings.end());
    std::printf("%s: %lld notes, %lld styles, cold %.3f ms; switch p50 %.3f ms, p95 %.3f ms, max %.3f ms\n",
        description, qlonglong(chart->notes().size()), qlonglong(cache.styles().size()),
        coldMs, timings[12], timings[22], timings.back());
}

void testPlayback(const QString& musicPath, const QString& soundFont, bool useVulkan)
{
    QTemporaryDir directory;
    SettingsService settings(std::make_unique<QSettingsStore>(directory.filePath(QStringLiteral("settings.ini"))));
    app::PlayerApplicationService player;
    visualization::VisualChartPtr chart;
    QObject::connect(&player, &app::PlayerApplicationService::visualizationReady, &player,
                     [&](const auto& value) { chart = value; });
    QString error;
    QObject::connect(&player, &app::PlayerApplicationService::errorOccurred, &player, [&](const QString& value) { error = value; });
    require(player.loadSoundFont(soundFont), "playback smoke test requires a valid SoundFont");
    presentation::MainWindow window(&player, &settings);
    presentation::settings::SettingsDialog dialog(&settings, &player, &window);
    auto* view = window.findChild<FallingNotesView*>();
    auto* combo = dialog.findChild<QComboBox*>(QStringLiteral("noteColorModeCombo"));
    if (useVulkan) settings.setGraphicsMode(settings::GraphicsMode::VulkanExperimental);
    window.show();
    player.openFile(musicPath);
    require(waitUntil([&] { return player.session() || !error.isEmpty(); }, 15000), "real playback session must load");
    if (!error.isEmpty()) std::fprintf(stderr, "%s\n", qPrintable(error));
    auto* session = player.session();
    require(session && player.durationMicroseconds() > 1'500'000, "session retention test must use a non-null, playable session");
    require(chart != nullptr, "playback smoke test must receive the real projected chart");
    benchmarkStyles(chart, "Real song");
    player.setPlaybackRatePercent(125);
    player.setMetronomeEnabled(true);
    int discontinuities = 0;
    QObject::connect(&player, &app::PlayerApplicationService::playbackDiscontinuity, &player, [&](auto) { ++discontinuities; });
    player.play();
    require(waitUntil([&] { return player.positionMicroseconds() > 80'000 || !error.isEmpty(); }), "real playback clock must advance");
    const auto id = window.winId();
    const int originalDiscontinuities = discontinuities;
    const auto initialPosition = player.positionMicroseconds();
    for (auto mode : {NoteColorMode::Normal, NoteColorMode::Vivid, NoteColorMode::Normal}) {
        combo->setCurrentIndex(combo->findData(int(mode)));
        QApplication::processEvents();
        require(player.session() == session && player.playbackState() == playback::State::Playing
                    && player.playbackRatePercent() == 125 && player.metronomeEnabled()
                    && view->noteColorMode() == mode && window.winId() == id
                    && discontinuities == originalDiscontinuities,
                "settings-driven colors must preserve real playback, window, rate and metronome");
    }
    require(waitUntil([&] { return player.positionMicroseconds() > initialPosition + 60'000; }),
            "the clock must continue advancing after recoloring");
    player.pause();
    require(waitUntil([&] { return player.playbackState() == playback::State::Paused; }),
            "the asynchronous pause must complete before capturing the playhead");
    const auto pausedAt = player.positionMicroseconds();
    settings.setNoteColorMode(NoteColorMode::Vivid);
    QApplication::processEvents();
    require(player.playbackState() == playback::State::Paused && player.positionMicroseconds() == pausedAt,
            "paused color switches must not move the playhead");
    player.seek(400'000);
    settings.setNoteColorMode(NoteColorMode::Normal);
    require(player.positionMicroseconds() == 400'000 && player.session() == session,
            "seek followed by recoloring must retain the session and seek target");
    bool sessionStatePreserved = false;
    QMetaObject::invokeMethod(session, [&] {
        sessionStatePreserved = session->playbackRatePercent() == 125 && session->metronomeEnabled()
            && session->state() == playback::State::Paused && session->positionMicroseconds() == 400'000;
    }, Qt::BlockingQueuedConnection);
    require(sessionStatePreserved, "the playback thread itself must retain rate, metronome and paused seek position");
#if MIDI_PLAY_HAS_VULKAN
    if (useVulkan) {
        FallingNotesVulkanWindow* gpu = nullptr;
        for (auto* candidate : QGuiApplication::allWindows())
            if (auto* found = qobject_cast<FallingNotesVulkanWindow*>(candidate)) gpu = found;
        require(gpu && gpu->isValid() && gpu->sceneState().noteColorMode == NoteColorMode::Normal,
                "real-session GPU test must actually render with the saved color mode");
    }
#endif
    player.stop();
    window.hide();
    require(error.isEmpty(), "theme/color switching must not raise audio errors");
    std::printf("Non-null session / %s playback, pause, seek, rate and metronome retention passed\n", useVulkan ? "Vulkan" : "Qt");
}

} // namespace

int main(int argc, char** argv)
{
    QApplication application(argc, argv);
    const auto args = application.arguments();
    const int snapshotIndex = args.indexOf(QStringLiteral("--snapshots"));
    const QString directory = snapshotIndex >= 0 ? args.value(snapshotIndex + 1) : QString();
    if (!directory.isEmpty()) require(QDir().mkpath(directory), "snapshot directory must be writable");
    testPersistence();
    testSaveFailureAndControls();
    const auto chart = sampleChart();
    testMaterials(chart);
    testCachesAndRaster(chart, directory);
#if MIDI_PLAY_HAS_VULKAN
    testVulkanRevisions(chart);
#endif
    if (args.contains(QStringLiteral("--benchmark"))) {
        benchmarkStyles(sampleChart(100'000, false), "Repeated");
        benchmarkStyles(sampleChart(100'000, true), "Varied");
    }
    const int playbackIndex = args.indexOf(QStringLiteral("--playback-smoke"));
    if (playbackIndex >= 0) {
        require(playbackIndex + 2 < args.size(), "--playback-smoke requires music and SoundFont paths");
        testPlayback(args[playbackIndex + 1], args[playbackIndex + 2], args.contains(QStringLiteral("--vulkan")));
    }
    std::puts("Note color persistence, materials, cache and rendering checks passed");
}
