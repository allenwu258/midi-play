#include "app/playerapplicationservice.h"
#include "infrastructure/musicxml/musicxmlreader.h"
#include "infrastructure/audio/fluidsynthengine.h"
#include "infrastructure/midi/midireader.h"
#include "infrastructure/readers/musicreaderregistry.h"
#include "infrastructure/readers/musicxmlreaderadapter.h"
#include "infrastructure/readers/midireaderadapter.h"
#include "presentation/mainwindow.h"
#include "domain/visualization/playbackscenestate.h"
#include "domain/visualization/playbackvisualizationprojector.h"
#include "domain/visualization/visiblenoteindex.h"
#include "presentation/visualization/fallingnotesrenderer.h"
#include "presentation/visualization/scenelayoutengine.h"

#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QDebug>
#include <QImage>
#include <QPainter>
#include <QThread>
#include <QTimer>

#include <algorithm>
#include <cstdio>

int main(int argc, char* argv[])
{
    const QString bundledPlatforms = QDir(QFileInfo(QString::fromLocal8Bit(argv[0])).absolutePath()).filePath(QStringLiteral("platforms"));
    if (QDir(bundledPlatforms).exists()) {
        QApplication::addLibraryPath(bundledPlatforms);
    }
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("MIDI Play"));
    app.setOrganizationName(QStringLiteral("MIDI Play"));

    midi_play::app::PlayerApplicationService service;

    if (argc > 2 && QString::fromLocal8Bit(argv[1]) == QStringLiteral("--audio-test")) {
        midi_play::audio::FluidSynthEngine engine;
        QString error;
        if (!engine.load(QString::fromLocal8Bit(argv[2]), &error) || !engine.configureTrack(0, 0, &error)) {
            qCritical().noquote() << error;
            return 1;
        }
        engine.start();
        engine.setTransportPosition(0);
        midi_play::playback::PlaybackEvent noteOn;
        noteOn.timestampUs = 100'000;
        noteOn.channel = 0;
        noteOn.pitch = 60;
        noteOn.velocity = 100;
        noteOn.kind = midi_play::playback::PlaybackEventKind::NoteOn;
        midi_play::playback::PlaybackEvent noteOff = noteOn;
        noteOff.timestampUs = 900'000;
        noteOff.kind = midi_play::playback::PlaybackEventKind::NoteOff;
        engine.submit(noteOn);
        engine.submit(noteOff);
        QTimer::singleShot(1'500, &app, &QCoreApplication::quit);
        return app.exec();
    }

    if (argc > 2 && QString::fromLocal8Bit(argv[1]) == QStringLiteral("--midi-test")) {
        const auto result = midi_play::midi::MidiReader().read(QString::fromLocal8Bit(argv[2]));
        if (!result.ok()) {
            qCritical().noquote() << result.error;
            return 1;
        }
        qInfo().noquote() << "MIDI loaded tracks=" << result.document->tracks().size()
                          << "duration_us=" << result.document->tickToMicroseconds(result.document->duration());
        return 0;
    }

    if (argc > 3 && QString::fromLocal8Bit(argv[1]) == QStringLiteral("--render-test")) {
        midi_play::readers::MusicReaderRegistry registry;
        registry.registerReader(std::make_unique<midi_play::readers::MusicXmlReaderAdapter>());
        registry.registerReader(std::make_unique<midi_play::readers::MidiReaderAdapter>());
        const QString inputPath = QString::fromLocal8Bit(argv[2]);
        const QString outputPath = QString::fromLocal8Bit(argv[3]);
        const auto* reader = registry.find(QFileInfo(inputPath).suffix());
        if (!reader) return 1;
        const auto result = reader->read(inputPath);
        if (!result.ok()) return 1;
        midi_play::visualization::VisualizationProjectionOptions options;
        options.fallbackTitle = QFileInfo(inputPath).completeBaseName();
        const auto chart = midi_play::visualization::PlaybackVisualizationProjector().project(
            *result.document, 1, options);
        if (!chart) return 1;
        const qint64 requestedPosition = argc > 4
            ? QString::fromLocal8Bit(argv[4]).toLongLong() : chart->durationUs() / 10;
        midi_play::visualization::PlaybackSceneState state;
        state.chart = chart;
        state.durationUs = chart->durationUs();
        state.transportPositionUs = std::clamp<qint64>(requestedPosition, 0, chart->durationUs());
        state.transportState = midi_play::playback::State::Playing;
        state.updateVisibleWindow();
        midi_play::visualization::VisibleNoteIndex index(chart->notes());
        QVector<int> candidates;
        index.query(state.visibleWindowStartUs, state.visibleWindowEndUs, candidates);
        state.candidateNoteIndices = std::span<const int>(candidates.constData(), candidates.size());
        const int renderWidth = argc > 5 ? std::clamp(QString::fromLocal8Bit(argv[5]).toInt(), 320, 7680) : 1280;
        const int renderHeight = argc > 6 ? std::clamp(QString::fromLocal8Bit(argv[6]).toInt(), 240, 4320) : 720;
        QImage image(renderWidth, renderHeight, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::transparent);
        QPainter painter(&image);
        const auto geometry = midi_play::presentation::visualization::SceneLayoutEngine().layout(
            image.size(), chart.get(), state.lookAheadUs);
        midi_play::presentation::visualization::FallingNotesRenderer renderer;
        renderer.render(painter, geometry, state);
        painter.end();
        std::fprintf(stdout, "render chart_tracks=%lld notes=%lld visible=%lld active=%lld position_us=%lld duration_us=%lld\n",
                     static_cast<long long>(chart->tracks().size()),
                     static_cast<long long>(chart->notes().size()),
                     static_cast<long long>(renderer.visibleNoteCount()),
                     static_cast<long long>(renderer.activeNoteCount()),
                     static_cast<long long>(state.transportPositionUs),
                     static_cast<long long>(state.durationUs));
        return image.save(outputPath) ? 0 : 1;
    }

    if (argc > 1) {
        midi_play::readers::MusicReaderRegistry registry;
        registry.registerReader(std::make_unique<midi_play::readers::MusicXmlReaderAdapter>());
        registry.registerReader(std::make_unique<midi_play::readers::MidiReaderAdapter>());
        const QString path = QString::fromLocal8Bit(argv[1]);
        const auto* reader = registry.find(QFileInfo(path).suffix());
        if (!reader) {
            qCritical().noquote() << "Unsupported music file extension:" << QFileInfo(path).suffix();
            return 1;
        }
        const auto result = reader->read(path);
        if (!result.ok()) {
            qCritical().noquote() << result.error;
            return 1;
        }
        qInfo().noquote() << "Loaded:" << result.document->title()
                          << "tracks=" << result.document->tracks().size()
                          << "duration_us=" << result.document->tickToMicroseconds(result.document->duration());
        return 0;
    }

    midi_play::presentation::MainWindow window(&service);
    window.show();

    const QString defaultSoundFontRelativePath = QStringLiteral("assets/midisound.sf2");
    QString defaultSoundFont =
        QDir(QCoreApplication::applicationDirPath()).filePath(defaultSoundFontRelativePath);
    if (!QFileInfo::exists(defaultSoundFont)) {
        defaultSoundFont = QDir::current().filePath(defaultSoundFontRelativePath);
    }
    service.loadSoundFont(defaultSoundFont);
    return app.exec();
}
