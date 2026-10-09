#pragma once

// GUI 绘制位置插值，只消费播放层样本，不驱动音频或修改音乐时间。
#include <QtGlobal>

#include <algorithm>
#include <chrono>

namespace midi_play::presentation::visualization {

// 以带时间戳的传输样本为锚点；无时间戳样本保持精确，供暂停和离屏确定性绘制。
class VisualPlaybackClock final {
public:
    // 使用与传输样本相同的 steady_clock 微秒基准。
    static qint64 monotonicMicroseconds()
    {
        return std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
    }

    // 接收权威位置样本，位置限制到非负文档时长。
    void sample(qint64 position, qint64 duration, qint64 sampledAtUs = 0)
    {
        m_duration = std::max<qint64>(0, duration);
        m_position = std::clamp(position, qint64(0), m_duration);
        m_sampledAt = sampledAtUs;
    }

    // 播放时将墙钟外推间隔限制到 100 ms，再按倍率换算音乐时间，防止无限漂移。
    qint64 position(qint64 now = monotonicMicroseconds()) const
    {
        const qint64 elapsed = m_playing && m_sampledAt > 0
            ? std::clamp(now - m_sampledAt, qint64(0), qint64(100'000)) : 0;
        return std::clamp(m_position + elapsed * m_rate / 100, qint64(0), m_duration);
    }

    // 切换外推开关；暂停时返回最近权威位置，暂停边界由新的精确样本校正。
    void setPlaying(bool playing) { m_playing = playing; }
    // 变速前先按旧倍率重锚，防止新倍率作用于旧样本全部经过时间。
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
