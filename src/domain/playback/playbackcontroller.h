#pragma once

#include "iplaybackaudioservice.h"
#include "playbacksession.h"

#include <QObject>
#include <memory>

namespace midi_play::playback {

class PlaybackController final : public QObject {
    Q_OBJECT
public:
    explicit PlaybackController(QObject* parent = nullptr);
    PlaybackSession* session() const { return m_session.get(); }

    bool setDocument(std::shared_ptr<const music::MusicDocument> document,
                     std::unique_ptr<IPlaybackAudioService> audioService,
                     QString* error);
    bool loadSoundFont(const QString& path, QString* error);

public slots:
    void play();
    void pause();
    void stop();
    void seek(qint64 microseconds);

signals:
    void stateChanged(midi_play::playback::State state);
    void positionChanged(qint64 position, qint64 duration);
    void errorOccurred(const QString& message);

private:
    std::unique_ptr<PlaybackSession> m_session;
};

} // namespace midi_play::playback
