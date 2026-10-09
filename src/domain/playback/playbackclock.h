#pragma once

// 软件单调传输时钟及其播放速率控制接口。
#include <QElapsedTimer>

namespace midi_play::playback {

// 基于单调计时器的软件传输时钟。
// 音频后端没有可用帧时钟时由它提供播放位置；后端时钟可以被会话采样，
// 而不会改变传输语义。
class PlaybackClock final {
public:
    // 清除位置、速率和运行状态。
    void reset();
    // 从指定位置开始运行，并重新建立单调计时器基准。
    void start(qint64 positionUs);
    // 在指定位置暂停。
    void pause(qint64 positionUs);
    // 定位到指定位置；运行中定位会重启计时器以消除旧基准影响。
    void seek(qint64 positionUs);
    // 设置播放速率，非法值被忽略并限制到安全范围。
    void setRate(double rate);
    // 返回当前播放速率。
    double rate() const { return m_rate; }
    // 返回当前播放位置（微秒）。
    qint64 positionUs() const;
    // 返回当前是否正在推进。
    bool isRunning() const { return m_running; }

private:
    QElapsedTimer m_timer;
    qint64 m_baseUs = 0;
    double m_rate = 1.0;
    bool m_running = false;
};

} // namespace midi_play::playback
