#pragma once
#include <QtGlobal>

namespace midi_play::app {

// Absolute-index timing; all products fit in qint64 within the 24-hour input limit.
class ExportTimeline final {
public:
    static constexpr qint64 maximumMusicUs = 24LL * 60 * 60 * 1'000'000;
    // Keep video audio on the same FluidSynth path as the existing MP3
    // exporter. 44.1 kHz also divides evenly into both supported frame rates.
    static constexpr int audioSampleRate = 44'100;
    ExportTimeline(qint64 musicEndUs, int fps, int ratePercent, int tailMs);
    bool valid() const { return m_frames > 0; }
    qint64 frameCount() const { return m_frames; }
    // The audio stream is padded to the exact duration represented by the
    // CFR video stream, with an integral number of samples per frame.
    qint64 sampleCount() const { return m_audioSamples; }
    qint64 videoDurationUs() const { return m_videoDurationUs; }
    qint64 musicPositionUs(qint64 frame) const;
    qint64 eventSample(qint64 musicUs) const;
private:
    int m_fps = 60;
    int m_rate = 100;
    qint64 m_videoDurationUs = 0;
    qint64 m_audioSamples = 0;
    qint64 m_frames = 0;
};
} // namespace midi_play::app
