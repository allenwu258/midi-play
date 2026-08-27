#include "app/playerapplicationservice.h"
#include "infrastructure/musicxml/musicxmlreader.h"
#include "infrastructure/audio/fluidsynthengine.h"
#include "infrastructure/midi/midireader.h"
#include "presentation/mainwindow.h"

#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QDebug>
#include <QThread>
#include <QTimer>

int main(int argc, char* argv[])
{
    const QString bundledPlatforms = QDir(QFileInfo(QString::fromLocal8Bit(argv[0])).absolutePath()).filePath(QStringLiteral("platforms"));
    if (QDir(bundledPlatforms).exists()) {
        qputenv("QT_QPA_PLATFORM_PLUGIN_PATH", bundledPlatforms.toUtf8());
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

    if (argc > 1) {
        const auto result = midi_play::musicxml::MusicXmlReader().read(QString::fromLocal8Bit(argv[1]));
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

    QString defaultSoundFont = QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("midiSound-2025-1-14.sf2"));
    if (!QFileInfo::exists(defaultSoundFont)) {
        defaultSoundFont = QDir::current().filePath(QStringLiteral("midiSound-2025-1-14.sf2"));
    }
    service.loadSoundFont(defaultSoundFont);
    return app.exec();
}
