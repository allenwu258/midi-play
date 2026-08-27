#pragma once

#include "playbacktypes.h"

#include <QString>

namespace midi_play::playback {

class IPlaybackAudioService {
public:
    virtual ~IPlaybackAudioService() = default;
    virtual bool loadSoundFont(const QString& path, QString* error) = 0;
    virtual bool configureTrack(const QString& trackId, int channel, int program, QString* error) = 0;
    virtual bool addTrack(const PlaybackData& data, QString* error) = 0;
    virtual bool start() = 0;
    virtual bool pause() = 0;
    virtual bool stop() = 0;
    virtual bool seek(qint64 positionUs) = 0;
    virtual bool flush() = 0;
    virtual void submit(const PlaybackEvent& event) = 0;
    virtual void submitOff(const PlaybackEvent& event) = 0;
};

} // namespace midi_play::playback
