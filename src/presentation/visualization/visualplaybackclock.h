#pragma once

#include <QtGlobal>

#include <algorithm>
#include <chrono>

namespace midi_play::presentation::visualization {

// GUI-thread clock anchored to timestamped transport samples. It never drives
// audio or changes musical timing. Untimestamped samples remain exact, which
// also makes paused exports and offscreen rendering deterministic.
class VisualPlaybackClock final {
public:
    static qint64 monotonicMicroseconds()
    {
        return std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
    }

    void sample(qint64 position, qint64 duration, qint64 sampledAtUs = 0)
    {
        m_duration = std::max<qint64>(0, duration);
        m_position = std::clamp(position, qint64(0), m_duration);
        m_sampledAt = sampledAtUs;
    }

    qint64 position(qint64 now = monotonicMicroseconds()) const
    {
        const qint64 elapsed = m_playing && m_sampledAt > 0
            ? std::clamp(now - m_sampledAt, qint64(0), qint64(100'000)) : 0;
        return std::clamp(m_position + elapsed * m_rate / 100, qint64(0), m_duration);
    }

    void setPlaying(bool playing) { m_playing = playing; }
    void setRate(int percent, qint64 now = monotonicMicroseconds())
    {
        if (m_sampledAt > 0 && m_playing) {
            m_position = position(now);
            m_sampledAt = now;
        }
        m_rate = std::clamp(percent, 20, 200);
    }

private:
    qint64 m_position = 0;
    qint64 m_duration = 0;
    qint64 m_sampledAt = 0;
    int m_rate = 100;
    bool m_playing = false;
};

} // namespace midi_play::presentation::visualization
