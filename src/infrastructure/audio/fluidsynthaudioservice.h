#pragma once

#include "domain/playback/iplaybackaudioservice.h"
#include "fluidsynthengine.h"

#include <memory>

namespace midi_play::audio {

class FluidSynthAudioService final : public playback::IPlaybackAudioService {
public:
    explicit FluidSynthAudioService(std::unique_ptr<FluidSynthEngine> engine);

    bool loadSoundFont(const QString& path, QString* error) override;
    bool configureTrack(const QString& trackId, int channel, int program, QString* error) override;
    bool addTrack(const playback::PlaybackData& data, QString* error) override;
    bool start() override;
    bool pause() override;
    bool stop() override;
    bool seek(qint64 positionUs) override;
    bool flush() override;
    void submit(const playback::PlaybackEvent& event) override;

private:
    std::unique_ptr<FluidSynthEngine> m_engine;
};

} // namespace midi_play::audio
