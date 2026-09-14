#include "playbackclock.h"

#include <algorithm>
#include <cmath>
#include <limits>

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

void PlaybackClock::setRate(double rate)
{
    if (!std::isfinite(rate) || rate <= 0.0) return;
    const double normalizedRate = std::clamp(rate, 0.01, 16.0);
    if (std::abs(m_rate - normalizedRate) < 1e-12) return;

    // Rebase before changing the multiplier so a live speed change never
    // introduces a position discontinuity.
    m_baseUs = positionUs();
    m_rate = normalizedRate;
    if (m_running) m_timer.restart();
}

qint64 PlaybackClock::positionUs() const
{
    if (!m_running || !m_timer.isValid()) return m_baseUs;
    const qint64 elapsedUs = m_timer.nsecsElapsed() / 1000;
    const long double scaled = static_cast<long double>(elapsedUs) * m_rate;
    const long double maximum = static_cast<long double>(
        std::numeric_limits<qint64>::max() - m_baseUs);
    if (scaled >= maximum) return std::numeric_limits<qint64>::max();
    return m_baseUs + static_cast<qint64>(scaled);
}

} // namespace midi_play::playback
