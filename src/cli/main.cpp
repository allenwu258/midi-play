#include "domain/visualization/playbackscenestate.h"
#include "domain/visualization/playbackvisualizationprojector.h"
#include "domain/visualization/visiblenoteindex.h"
#include "infrastructure/audio/fluidsynthengine.h"
#include "infrastructure/midi/midireader.h"
#include "infrastructure/readers/midireaderadapter.h"
#include "infrastructure/readers/musicreaderregistry.h"
#include "infrastructure/readers/musicxmlreaderadapter.h"
#include "presentation/visualization/fallingnotesrenderer.h"
#include "presentation/visualization/scenelayoutengine.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QPainter>
#include <QTimer>

#include <algorithm>
#include <cstdio>
#include <memory>

namespace {

void printUsage(FILE* stream)
{
    std::fputs(
        "Usage: midi_play_cli <musicxml|mxl|mid|midi|kar file>\n"
        "       midi_play_cli --audio-test <sf2|sf3 file> [replacement SoundFont]\n"
        "       midi_play_cli --midi-test <MIDI file>\n"
        "       midi_play_cli --render-test <music file> <output.png> [position_us [width [height]]] [--theme dark|light] [--note-colors normal|vivid]\n"
        "       midi_play_cli --help\n",
        stream);
}

void registerReaders(midi_play::readers::MusicReaderRegistry& registry)
{
    registry.registerReader(std::make_unique<midi_play::readers::MusicXmlReaderAdapter>());
    registry.registerReader(std::make_unique<midi_play::readers::MidiReaderAdapter>());
}

int runAudioTest(const QStringList& arguments, QCoreApplication& application)
{
    midi_play::audio::FluidSynthEngine engine;
    QString error;
    if (!engine.load(arguments.at(2), &error) || !engine.configureTrack(0, 0, &error)) {
        qCritical().noquote() << error;
        return 1;
    }
    if (arguments.size() == 4
        && (!engine.load(arguments.at(3), &error) || !engine.configureTrack(0, 0, &error))) {
        qCritical().noquote() << error;
        return 1;
    }

    midi_play::playback::PlaybackEvent noteOn;
    noteOn.channel = 0;
    noteOn.pitch = 60;
    noteOn.velocity = 100;
    noteOn.kind = midi_play::playback::PlaybackEventKind::NoteOn;
    midi_play::playback::PlaybackEvent noteOff = noteOn;
    noteOff.kind = midi_play::playback::PlaybackEventKind::NoteOff;
    engine.start();
    engine.setTransportPosition(0);
    engine.submit(noteOn);
    QTimer::singleShot(900, &application, [&engine, noteOff] { engine.submit(noteOff); });
    QTimer::singleShot(1500, &application, &QCoreApplication::quit);
    return application.exec();
}

int runMidiTest(const QString& inputPath)
{
    const auto result = midi_play::midi::MidiReader().read(inputPath);
    if (!result.ok()) {
        qCritical().noquote() << result.error;
        return 1;
    }
    qInfo().noquote() << "MIDI loaded tracks=" << result.document->tracks().size()
                      << "duration_us="
                      << result.document->tickToMicroseconds(result.document->duration());
    return 0;
}

int runRenderTest(QStringList arguments)
{
    auto mode = midi_play::settings::kDefaultThemeMode;
    auto colorMode = midi_play::settings::kDefaultNoteColorMode;
    const int themeArgument = arguments.indexOf(QStringLiteral("--theme"));
    if (themeArgument >= 0) {
        const QString value = arguments.value(themeArgument + 1);
        if (themeArgument < 4 || arguments.count(QStringLiteral("--theme")) != 1
            || (value != QStringLiteral("dark") && value != QStringLiteral("light"))) {
            qCritical() << "--theme requires exactly one value: dark or light";
            return 2;
        }
        mode = value == QStringLiteral("light")
            ? midi_play::settings::ThemeMode::Light : midi_play::settings::ThemeMode::Dark;
        arguments.removeAt(themeArgument + 1);
        arguments.removeAt(themeArgument);
    }
    const int colorArgument = arguments.indexOf(QStringLiteral("--note-colors"));
    if (colorArgument >= 0) {
        const QString value = arguments.value(colorArgument + 1);
        if (colorArgument < 4 || arguments.count(QStringLiteral("--note-colors")) != 1
            || (value != QStringLiteral("normal") && value != QStringLiteral("vivid"))) {
            qCritical() << "--note-colors requires exactly one value: normal or vivid";
            return 2;
        }
        colorMode = value == QStringLiteral("normal")
            ? midi_play::settings::NoteColorMode::Normal : midi_play::settings::NoteColorMode::Vivid;
        arguments.removeAt(colorArgument + 1);
        arguments.removeAt(colorArgument);
    }
    if (arguments.size() < 4 || arguments.size() > 7) {
        printUsage(stderr);
        return 2;
    }
    midi_play::readers::MusicReaderRegistry registry;
    registerReaders(registry);
    const QString inputPath = arguments.at(2);
    const QString outputPath = arguments.at(3);
    const auto* reader = registry.find(QFileInfo(inputPath).suffix());
    if (!reader) {
        qCritical().noquote() << "Unsupported music file extension:" << QFileInfo(inputPath).suffix();
        return 1;
    }
    const auto result = reader->read(inputPath);
    if (!result.ok()) {
        qCritical().noquote() << result.error;
        return 1;
    }
    midi_play::visualization::VisualizationProjectionOptions options;
    options.fallbackTitle = QFileInfo(inputPath).completeBaseName();
    const auto chart = midi_play::visualization::PlaybackVisualizationProjector().project(
        *result.document, 1, options);
    if (!chart) {
        qCritical() << "Unable to create playback visualization";
        return 1;
    }
    const qint64 requestedPosition = arguments.size() > 4
        ? arguments.at(4).toLongLong() : chart->durationUs() / 10;
    midi_play::visualization::PlaybackSceneState state;
    state.themeMode = mode;
    state.noteColorMode = colorMode;
    state.chart = chart;
    state.durationUs = chart->durationUs();
    state.transportPositionUs = std::clamp<qint64>(requestedPosition, 0, chart->durationUs());
    state.transportState = midi_play::playback::State::Playing;
    state.updateVisibleWindow();
    midi_play::visualization::VisibleNoteIndex index(chart->notes());
    QVector<int> candidates;
    index.query(state.visibleWindowStartUs, state.visibleWindowEndUs, candidates);
    state.candidateNoteIndices = std::span<const int>(candidates.constData(), candidates.size());
    const int width = arguments.size() > 5 ? std::clamp(arguments.at(5).toInt(), 320, 7680) : 1280;
    const int height = arguments.size() > 6 ? std::clamp(arguments.at(6).toInt(), 240, 4320) : 720;
    QImage image(width, height, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    const auto geometry = midi_play::presentation::visualization::SceneLayoutEngine().layout(
        image.size(), chart.get(), state.lookAheadUs);
    midi_play::presentation::visualization::FallingNotesRenderer renderer;
    renderer.render(painter, geometry, state);
    painter.end();
    std::fprintf(stdout,
                 "render chart_tracks=%lld notes=%lld visible=%lld active=%lld position_us=%lld duration_us=%lld\n",
                 static_cast<long long>(chart->tracks().size()),
                 static_cast<long long>(chart->notes().size()),
                 static_cast<long long>(renderer.visibleNoteCount()),
                 static_cast<long long>(renderer.activeNoteCount()),
                 static_cast<long long>(state.transportPositionUs),
                 static_cast<long long>(state.durationUs));
    return image.save(outputPath) ? 0 : 1;
}

int runReaderSmokeTest(const QString& inputPath)
{
    midi_play::readers::MusicReaderRegistry registry;
    registerReaders(registry);
    const auto* reader = registry.find(QFileInfo(inputPath).suffix());
    if (!reader) {
        qCritical().noquote() << "Unsupported music file extension:" << QFileInfo(inputPath).suffix();
        return 1;
    }
    const auto result = reader->read(inputPath);
    if (!result.ok()) {
        qCritical().noquote() << result.error;
        return 1;
    }
    qInfo().noquote() << "Loaded:" << result.document->title()
                      << "tracks=" << result.document->tracks().size()
                      << "duration_us="
                      << result.document->tickToMicroseconds(result.document->duration());
    return 0;
}

} // namespace

int main(int argc, char* argv[])
{
    const bool renderMode = argc > 1 && QByteArray(argv[1]) == QByteArrayLiteral("--render-test");
    std::unique_ptr<QCoreApplication> application;
    if (renderMode) {
        const QString executableDirectory = QFileInfo(QString::fromLocal8Bit(argv[0])).absolutePath();
        const QString bundledPlatforms = QDir(executableDirectory).filePath(QStringLiteral("platforms"));
        if (QDir(bundledPlatforms).exists()) {
            QCoreApplication::addLibraryPath(bundledPlatforms);
        }
        application = std::make_unique<QGuiApplication>(argc, argv);
    } else {
        application = std::make_unique<QCoreApplication>(argc, argv);
    }
    application->setApplicationName(QStringLiteral("MidiPlayCli"));
    application->setOrganizationName(QStringLiteral("MidiPlay"));
    const QStringList arguments = application->arguments();
    const QString command = arguments.value(1);
    if (arguments.size() == 1 || command == QStringLiteral("--help")
        || command == QStringLiteral("-h")) {
        printUsage(stdout);
        return 0;
    }
    if (command == QStringLiteral("--audio-test") && arguments.size() >= 3 && arguments.size() <= 4) {
        return runAudioTest(arguments, *application);
    }
    if (command == QStringLiteral("--midi-test") && arguments.size() == 3) {
        return runMidiTest(arguments.at(2));
    }
    if (command == QStringLiteral("--render-test")) {
        return runRenderTest(arguments);
    }
    if (!command.startsWith(QLatin1Char('-')) && arguments.size() == 2) {
        return runReaderSmokeTest(command);
    }
    printUsage(stderr);
    return 2;
}
