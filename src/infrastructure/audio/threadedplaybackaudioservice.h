#pragma once

#include "domain/playback/iplaybackaudioservice.h"

#include <QThread>
#include <memory>

namespace midi_play::audio {

class ThreadedPlaybackAudioService final : public playback::IPlaybackAudioService {
public:
    explicit ThreadedPlaybackAudioService(std::unique_ptr<playback::IPlaybackAudioService> service);
    ~ThreadedPlaybackAudioService() override;

    bool loadSoundFont(const QString& path, QString* error) override;
    bool configureTrack(const QString& trackId, int channel, int program, QString* error) override;
    bool addTrack(const playback::PlaybackData& data, QString* error) override;
    bool start() override;
    bool pause() override;
    bool stop() override;
    bool seek(qint64 positionUs) override;
    bool setTransportPosition(qint64 positionUs) override;
    qint64 clockPositionUs() const override;
    bool supportsTimedEvents() const override;
    bool flush() override;
    void submit(const playback::PlaybackEvent& event) override;
    void submitOff(const playback::PlaybackEvent& event) override;
    void submitBatch(const QVector<playback::PlaybackEvent>& events) override;

private:
    class Worker;
    std::unique_ptr<playback::IPlaybackAudioService> m_service;
    std::unique_ptr<Worker> m_worker;
    QThread m_thread;
};

} // namespace midi_play::audio
