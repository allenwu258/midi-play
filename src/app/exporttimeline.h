#pragma once
// 导出时间映射：以绝对视频帧/音频样本索引统一倍率、尾音及固定帧率。
#include <QtGlobal>

namespace midi_play::app {

// 在 24 小时输入限制内使用 qint64 整数乘积，避免逐帧累加造成漂移。
class ExportTimeline final {
public:
    static constexpr qint64 maximumMusicUs = 24LL * 60 * 60 * 1'000'000;
    // 视频音频沿用 MP3 的 FluidSynth 路径；44.1 kHz 可被 30/60 FPS 整除。
    static constexpr int audioSampleRate = 44'100;
    // 校验时长、30/60 FPS、20%..200% 倍率及 0..5000 ms 尾音；无效时帧数为零。
    ExportTimeline(qint64 musicEndUs, int fps, int ratePercent, int tailMs);
    // 查询有效性与向上量化到完整视频帧后的帧数。
    bool valid() const { return m_frames > 0; }
    qint64 frameCount() const { return m_frames; }
    // PCM 补齐到 CFR 视频的精确帧时长，每视频帧对应整数个音频样本帧。
    qint64 sampleCount() const { return m_audioSamples; }
    // 视频时长向上取整为微秒，供最终媒体校验使用。
    qint64 videoDurationUs() const { return m_videoDurationUs; }
    // 按绝对帧索引反算音乐微秒位置，尾音阶段允许超出原乐曲结束时间。
    qint64 musicPositionUs(qint64 frame) const;
    // 将音乐事件微秒位置按倍率映射到最近的音频样本帧。
    qint64 eventSample(qint64 musicUs) const;
private:
    int m_fps = 60;
    int m_rate = 100;
    qint64 m_videoDurationUs = 0;
    qint64 m_audioSamples = 0;
    qint64 m_frames = 0;
};
} // namespace midi_play::app
