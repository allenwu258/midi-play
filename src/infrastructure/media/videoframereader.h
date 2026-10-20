#pragma once
#include <QImage>
#include <QString>
#include <atomic>
#include <memory>

namespace midi_play::media {
struct VideoMetadata {
    qint64 durationUs = 0;
    // Absolute source timestamps; Qt positions are relative to mediaStartTimeUs.
    qint64 startTimeUs = 0;
    qint64 mediaStartTimeUs = 0;
    // First audio track's half-open interval, relative to the first video frame.
    qint64 audioStartUs = 0;
    qint64 audioEndUs = 0;
    QSize sourceSize;
    bool hasAudio = false;
};
struct VideoFrame {
    QImage image;
    qint64 positionUs = 0;
};

// Thread-confined, bounded-memory decoder. Callers own scheduling and looping.
// Sequential requests reuse the decoder; random access seeks to a keyframe and
// decodes forward by presentation timestamps, including B-frames and VFR media.
class VideoFrameReader final {
public:
    VideoFrameReader();
    ~VideoFrameReader();
    VideoFrameReader(const VideoFrameReader&) = delete;
    VideoFrameReader& operator=(const VideoFrameReader&) = delete;
    bool open(const QString& path, int maximumDimension, QString* error,
              const std::atomic_bool* canceled = nullptr);
    VideoMetadata metadata() const;
    VideoFrame frameAt(qint64 positionUs, QString* error,
                       const std::atomic_bool* canceled = nullptr);
private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
};
}
