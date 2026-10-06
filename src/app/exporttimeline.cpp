#include "exporttimeline.h"

namespace midi_play::app {
ExportTimeline::ExportTimeline(qint64 musicEndUs, int fps, int ratePercent, int tailMs)
    : m_fps(fps), m_rate(ratePercent)
{
    if (musicEndUs <= 0 || musicEndUs > maximumMusicUs || (fps != 30 && fps != 60)
        || ratePercent < 20 || ratePercent > 200 || tailMs < 0 || tailMs > 5000) return;
    // Speed scales musical time; the release tail stays in wall-clock time.
    const qint64 durationNumerator = musicEndUs * 100 + qint64(tailMs) * 1000 * m_rate;

    // Quantize the rational duration once. Rounding to microseconds first
    // would add a frame at exact boundaries such as 1 s / 120% at 60 FPS.
    const qint64 frameDenominator = 1'000'000LL * m_rate;
    m_frames = (durationNumerator * fps + frameDenominator - 1) / frameDenominator;
    m_videoDurationUs = (m_frames * 1'000'000 + fps - 1) / fps;
    // 30/60 FPS divide 44.1 kHz exactly. Match PCM to complete video frames.
    m_audioSamples = m_frames * (audioSampleRate / m_fps);
}
qint64 ExportTimeline::musicPositionUs(qint64 frame) const
{
    if (frame <= 0) return 0;
    return (frame * 1'000'000LL * m_rate + m_fps * 100LL / 2) / (m_fps * 100LL);
}
qint64 ExportTimeline::eventSample(qint64 musicUs) const
{
    const qint64 denominator = 1'000'000LL * m_rate;
    return (musicUs * audioSampleRate * 100LL + denominator / 2) / denominator;
}
} // namespace midi_play::app
