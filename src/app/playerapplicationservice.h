#pragma once

#include "domain/playback/playbackcontroller.h"
#include "domain/visualization/visualchart.h"
#include "infrastructure/readers/musicreaderregistry.h"

#include <QObject>
#include <memory>

namespace midi_play::app {

class PlayerApplicationService final : public QObject {
    Q_OBJECT
public:
    explicit PlayerApplicationService(QObject* parent = nullptr);

    playback::PlaybackSession* session() const { return m_controller ? m_controller->session() : nullptr; }
    QString fileName() const { return m_fileName; }
    playback::State playbackState() const { return m_playbackState; }
    qint64 positionMicroseconds() const { return m_positionUs; }
    qint64 durationMicroseconds() const { return m_durationUs; }

public slots:
    void openFile(const QString& path);
    void openMusicXml(const QString& path);
    void loadSoundFont(const QString& path);
    void play();
    void pause();
    void stop();
    void seek(qint64 microseconds);

signals:
    void busyChanged(bool busy);
    void documentLoaded(const QString& title, qint64 duration);
    void soundFontLoaded(const QString& path);
    void positionChanged(qint64 position, qint64 duration);
    void playbackStateChanged(midi_play::playback::State state);
    void visualizationReady(midi_play::visualization::VisualChartPtr chart);
    void errorOccurred(const QString& message);

private:
    void connectSession();

    std::unique_ptr<playback::PlaybackController> m_controller;
    QString m_fileName;
    QString m_soundFontPath;
    readers::MusicReaderRegistry m_readerRegistry;
    playback::State m_playbackState = playback::State::Empty;
    qint64 m_positionUs = 0;
    qint64 m_durationUs = 0;
    quint64 m_loadGeneration = 0;
};

} // namespace midi_play::app
