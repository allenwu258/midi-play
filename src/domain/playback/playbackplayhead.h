#pragma once

#include "playbackclock.h"

namespace midi_play::playback {

enum class PlayHeadState {
    Stopped,
    Playing,
    Paused
};

// Transport state shared by playback scheduling and future audio-frame
// renderers. The software clock is a fallback; an audio backend may publish a
// frame-derived position through setAudioPositionUs().
class PlaybackPlayHead final {
public:
    void configure(int sampleRate);
    void reset();
    void start(qint64 positionUs);
    void pause(qint64 positionUs);
    void seek(qint64 positionUs);
    void setAudioPositionUs(qint64 positionUs);
    void clearAudioPosition();

    PlayHeadState state() const { return m_state; }
    qint64 positionUs() const;
    qint64 framePosition() const;
    int sampleRate() const { return m_sampleRate; }

private:
    PlaybackClock m_clock;
    qint64 m_audioPositionUs = -1;
    int m_sampleRate = 48000;
    PlayHeadState m_state = PlayHeadState::Stopped;
};

} // namespace midi_play::playback
