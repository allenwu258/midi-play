#include "playbackplayhead.h"

// 实现播放头状态、音频时钟覆盖和帧位置换算。
#include <algorithm>

namespace midi_play::playback {

// 采样率只影响 framePosition，不影响微秒传输时钟。
void PlaybackPlayHead::configure(int sampleRate)
{
    m_sampleRate = std::max(1, sampleRate);
}

// 播放头重置后必须重新由 start 建立时钟基准。
void PlaybackPlayHead::reset()
{
    m_clock.reset();
    m_audioPositionUs = -1;
    m_state = PlayHeadState::Stopped;
}

// 开始播放时丢弃旧硬件位置，避免新一轮传输误用上一轮帧时钟。
void PlaybackPlayHead::start(qint64 positionUs)
{
    m_audioPositionUs = -1;
    m_clock.start(positionUs);
    m_state = PlayHeadState::Playing;
}

// 暂停同时把软件时钟和硬件位置固定在同一个非负位置。
void PlaybackPlayHead::pause(qint64 positionUs)
{
    const qint64 resolved = std::max<qint64>(0, positionUs);
    m_clock.pause(resolved);
    m_audioPositionUs = resolved;
    m_state = PlayHeadState::Paused;
}

// 定位时保留当前时钟是否运行，由 PlaybackClock 负责处理重基准。
void PlaybackPlayHead::seek(qint64 positionUs)
{
    const qint64 resolved = std::max<qint64>(0, positionUs);
    m_clock.seek(resolved);
    m_audioPositionUs = resolved;
}

// 速率由底层单调时钟统一校验和限制。
void PlaybackPlayHead::setRate(double rate)
{
    m_clock.setRate(rate);
}

// 仅接受非负的后端位置；负值用于表示读取失败或暂不可用。
void PlaybackPlayHead::setAudioPositionUs(qint64 positionUs)
{
    if (positionUs < 0) {
        m_audioPositionUs = -1;
        return;
    }
    m_audioPositionUs = positionUs;
}

// 强制回退软件时钟。
void PlaybackPlayHead::clearAudioPosition()
{
    m_audioPositionUs = -1;
}

// 优先使用硬件帧时钟，保证音频输出与事件调度对齐。
qint64 PlaybackPlayHead::positionUs() const
{
    if (m_audioPositionUs >= 0) return m_audioPositionUs;
    return m_clock.positionUs();
}

// 使用整数除法得到当前已播放的音频帧数。
qint64 PlaybackPlayHead::framePosition() const
{
    return positionUs() * static_cast<qint64>(m_sampleRate) / 1'000'000;
}

} // namespace midi_play::playback
