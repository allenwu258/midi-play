#include "playbackclock.h"

// 实现可暂停、可定位、可变速的单调微秒时钟。
#include <algorithm>
#include <cmath>
#include <limits>

namespace midi_play::playback {

// 恢复初始停止状态；invalidate 让下一次 start 建立新的计时基准。
void PlaybackClock::reset()
{
    m_baseUs = 0;
    m_running = false;
    m_timer.invalidate();
}

// 位置统一限制为非负，restart 保证 elapsed 从零开始计算。
void PlaybackClock::start(qint64 positionUs)
{
    m_baseUs = std::max<qint64>(0, positionUs);
    m_timer.restart();
    m_running = true;
}

// 暂停时把调用方采样的位置固化为新的基准值。
void PlaybackClock::pause(qint64 positionUs)
{
    m_baseUs = std::max<qint64>(0, positionUs);
    m_running = false;
}

// 定位保持当前运行状态；运行中的定位需要重新启动计时器。
void PlaybackClock::seek(qint64 positionUs)
{
    m_baseUs = std::max<qint64>(0, positionUs);
    if (m_running) m_timer.restart();
}

// 速率变化前先把当前位置重新基准化，避免倍率切换产生跳变。
void PlaybackClock::setRate(double rate)
{
    if (!std::isfinite(rate) || rate <= 0.0) return;
    const double normalizedRate = std::clamp(rate, 0.01, 16.0);
    if (std::abs(m_rate - normalizedRate) < 1e-12) return;

    m_baseUs = positionUs();
    m_rate = normalizedRate;
    if (m_running) m_timer.restart();
}

// 将单调计时器经过倍率缩放的 elapsed 转换为微秒，并处理 qint64 溢出。
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
