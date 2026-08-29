#pragma once

#include "iplaybackaudioservice.h"
#include "playbacksession.h"
#include "playbackpositionthrottler.h"

#include <QObject>
#include <QTimer>
#include <memory>
#include <QThread>

namespace midi_play::playback {

class PlaybackController final : public QObject {
    Q_OBJECT
public:
    explicit PlaybackController(QObject* parent = nullptr);
    ~PlaybackController() override;
    PlaybackSession* session() const { return m_session.get(); }
    int positionPublishRate() const noexcept { return m_positionPublishRate; }

    bool setDocument(std::shared_ptr<const music::MusicDocument> document,
                     std::unique_ptr<IPlaybackAudioService> audioService,
                     QString* error);
    bool loadSoundFont(const QString& path, QString* error);
    void setPositionPublishRate(int refreshRate);

public slots:
    void play();
    void pause();
    void stop();
    void seek(qint64 microseconds);

signals:
    void stateChanged(midi_play::playback::State state);
    // Coalesced transport samples, normally published once per controller
    // frame interval. Transport boundaries may publish an immediate endpoint;
    // audio scheduling remains owned by PlaybackSession.
    void positionChanged(qint64 position, qint64 duration);
    void errorOccurred(const QString& message);

private:
    void flushPositionUpdate();

private slots:
    void onPositionTimer();

private:
    std::unique_ptr<PlaybackSession> m_session;
    QThread m_playbackThread;
    QTimer m_positionTimer;
    PlaybackPositionThrottler m_positionThrottler;
    int m_positionPublishRate = 60;
};

} // namespace midi_play::playback
