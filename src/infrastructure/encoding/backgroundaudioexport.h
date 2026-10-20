#pragma once
#include <QString>
#include <atomic>
#include "infrastructure/media/videoframereader.h"

namespace midi_play::encoding {
// Render one bounded audio cycle to disk, then loop and mix in output time.
// Temporary files belong to the caller's export directory; canceled work is discarded.
bool mixBackgroundVideoAudio(const QString& executable, const QString& videoPath,
    const media::VideoMetadata& video, qint64 musicDurationUs, int ratePercent, int sampleRate,
    qint64 totalSamples, const QString& temporaryDirectory, QString& musicPcmPath,
    const std::atomic_bool* canceled, QString* error);
}
