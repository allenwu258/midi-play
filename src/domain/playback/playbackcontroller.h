#pragma once

#include "iplaybackaudioservice.h"
#include "playbacksession.h"
#include "playbackpositionthrottler.h"
#include "domain/settings/playersettings.h"

#include <QObject>
#include <QChronoTimer>
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
    int playbackRatePercent() const noexcept { return m_playbackRatePercent; }

    bool setDocument(std::shared_ptr<const music::MusicDocument> document,
                     std::unique_ptr<IPlaybackAudioService> audioService,
                     QString* error);
    bool loadSoundFont(const QString& path, QString* error);
    void loadSoundFontAsync(const QString& path);
    void setPositionPublishRate(int refreshRate);
    bool setPlaybackRatePercent(int percent);
    void setMetronomeEnabled(bool enabled);
    bool metronomeEnabled() const noexcept { return m_metronomeEnabled; }
    bool supportsMetronome() const noexcept { return m_supportsMetronome; }
    const QString& metronomeUnavailableReason() const { return m_metronomeUnavailableReason; }

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
    void soundFontLoadFinished(bool success, const QString& error);
    void metronomeAvailabilityChanged(bool available, const QString& reason);

private:
    void flushPositionUpdate();

private slots:
    void onPositionTimer();

private:
    std::unique_ptr<PlaybackSession> m_session;
    QThread m_playbackThread;
    QChronoTimer m_positionTimer;
    PlaybackPositionThrottler m_positionThrottler;
    int m_positionPublishRate = 60;
    int m_playbackRatePercent = midi_play::settings::kDefaultPlaybackRatePercent;
    bool m_supportsPlaybackRate = true;
    bool m_supportsMetronome = false;
    bool m_metronomeEnabled = false;
    QString m_metronomeUnavailableReason;
};

} // namespace midi_play::playback
