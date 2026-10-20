#pragma once
#include <QImage>
#include <QString>
#include <atomic>
#include <memory>

namespace midi_play::media {
struct VideoMetadata {
    qint64 durationUs = 0;
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
