#pragma once

#include <QElapsedTimer>

namespace midi_play::playback {

// Monotonic transport clock used when an audio backend cannot expose an
// audio-frame clock. A backend clock can be sampled by the session without
// changing transport semantics.
class PlaybackClock final {
public:
    void reset();
    void start(qint64 positionUs);
    void pause(qint64 positionUs);
    void seek(qint64 positionUs);
    void setRate(double rate);
    double rate() const { return m_rate; }
    qint64 positionUs() const;
    bool isRunning() const { return m_running; }

private:
    QElapsedTimer m_timer;
    qint64 m_baseUs = 0;
    double m_rate = 1.0;
    bool m_running = false;
};

} // namespace midi_play::playback
