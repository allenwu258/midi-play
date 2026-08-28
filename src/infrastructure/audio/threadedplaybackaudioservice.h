#pragma once

#include "domain/playback/iplaybackaudioservice.h"

#include <QThread>
#include <atomic>
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
    playback::PlaybackClockSource clockSource() const override;
    bool supportsTimedEvents() const override;
    bool supportsPerNoteExpression() const override;
    playback::PlaybackBackendCapabilities capabilities() const override;
    bool flush() override;
    void setEventGeneration(quint64 generation) override;
    void submit(const playback::PlaybackEvent& event) override;
    void submitOff(const playback::PlaybackEvent& event) override;
    void submitBatch(const QVector<playback::PlaybackEvent>& events) override;
    void submitBatch(const QVector<playback::PlaybackEvent>& events, quint64 generation) override;

private:
    class Worker;
    const std::shared_ptr<const playback::PlaybackClockSnapshot> m_clockSnapshot;
    const playback::PlaybackBackendCapabilities m_capabilities;
    QThread m_thread;
    std::unique_ptr<Worker> m_worker;
    std::atomic<quint64> m_submissionGeneration {0};
};

} // namespace midi_play::audio
