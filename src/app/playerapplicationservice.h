#pragma once

#include "domain/playback/playbackcontroller.h"
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
    void errorOccurred(const QString& message);

private:
    void connectSession();

    std::unique_ptr<playback::PlaybackController> m_controller;
    QString m_fileName;
    QString m_soundFontPath;
    readers::MusicReaderRegistry m_readerRegistry;
};

} // namespace midi_play::app
