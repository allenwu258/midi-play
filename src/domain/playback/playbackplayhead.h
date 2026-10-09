#pragma once

// 结合软件时钟和可选音频硬件时钟的统一播放头。
#include "playbackclock.h"

namespace midi_play::playback {

// 播放头的传输状态。
enum class PlayHeadState {
    Stopped,
    Playing,
    Paused
};

// 在调度器和未来音频帧渲染器之间共享的播放头。
// 软件时钟是回退方案，音频后端可通过 setAudioPositionUs() 提供帧位置。
class PlaybackPlayHead final {
public:
    // 设置采样率；无效值归一化为至少 1。
    void configure(int sampleRate);
    // 清除时钟、音频位置和状态。
    void reset();
    // 从指定位置开始播放，并优先使用软件时钟。
    void start(qint64 positionUs);
    // 固化指定位置并切换到 Paused。
    void pause(qint64 positionUs);
    // 定位到指定位置，保持当前播放/暂停语义。
    void seek(qint64 positionUs);
    // 设置软件时钟速率。
    void setRate(double rate);
    // 接收有效的音频硬件位置；负值表示无效。
    void setAudioPositionUs(qint64 positionUs);
    // 清除硬件位置，使 positionUs() 回退到软件时钟。
    void clearAudioPosition();

    // 返回播放头状态。
    PlayHeadState state() const { return m_state; }
    // 返回硬件时钟（若有效）或软件时钟的位置。
    qint64 positionUs() const;
    // 按采样率把微秒位置换算为音频帧位置。
    qint64 framePosition() const;
    int sampleRate() const { return m_sampleRate; }
    double rate() const { return m_clock.rate(); }

private:
    PlaybackClock m_clock;
    qint64 m_audioPositionUs = -1;
    int m_sampleRate = 48000;
    PlayHeadState m_state = PlayHeadState::Stopped;
};

} // namespace midi_play::playback
