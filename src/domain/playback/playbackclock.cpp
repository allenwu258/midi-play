#include "playbackclock.h"

#include <algorithm>

namespace midi_play::playback {

void PlaybackClock::reset()
{
    m_baseUs = 0;
    m_running = false;
    m_timer.invalidate();
}

void PlaybackClock::start(qint64 positionUs)
{
    m_baseUs = std::max<qint64>(0, positionUs);
    m_timer.restart();
    m_running = true;
}

void PlaybackClock::pause(qint64 positionUs)
{
    m_baseUs = std::max<qint64>(0, positionUs);
    m_running = false;
}

void PlaybackClock::seek(qint64 positionUs)
{
    m_baseUs = std::max<qint64>(0, positionUs);
    if (m_running) m_timer.restart();
}

qint64 PlaybackClock::positionUs() const
{
    if (!m_running || !m_timer.isValid()) return m_baseUs;
    return m_baseUs + m_timer.nsecsElapsed() / 1000;
}

} // namespace midi_play::playback
