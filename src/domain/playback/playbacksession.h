#pragma once

#include "domain/music/musicdocument.h"
#include "playbacktypes.h"
#include "iplaybackaudioservice.h"
#include "playbackmodel.h"
#include "playbackeventscheduler.h"
#include "playbackclock.h"

#include <QObject>
#include <QTimer>
#include <memory>

namespace midi_play::playback {

class PlaybackSession final : public QObject {
    Q_OBJECT
public:
    explicit PlaybackSession(std::shared_ptr<const music::MusicDocument> document,
                             std::unique_ptr<IPlaybackAudioService> audioService,
                             QObject* parent = nullptr);
    ~PlaybackSession() override;

    State state() const { return m_state; }
    qint64 positionMicroseconds() const { return m_positionUs; }
    qint64 durationMicroseconds() const;
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

private slots:
    void onTimer();

private:
    void setState(State state);
    void emitPosition();
    void flushActiveNotes();
    void rebuildAudioState(qint64 targetUs);

    std::shared_ptr<const music::MusicDocument> m_document;
    std::unique_ptr<IPlaybackAudioService> m_audioService;
    PlaybackModel m_playbackModel;
    QTimer* m_timer = nullptr;
    State m_state = State::Ready;
    qint64 m_positionUs = 0;
    qint64 m_clockBaseUs = 0;
    PlaybackClock m_clock;
    PlaybackEventScheduler m_scheduler;
};

} // namespace midi_play::playback
