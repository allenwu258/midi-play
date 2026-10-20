#pragma once
#include <QString>
#include <atomic>

namespace midi_play::encoding {
// Render one bounded audio cycle to disk, then loop and mix in output time.
// Temporary files belong to the caller's export directory; canceled work is discarded.
bool mixBackgroundVideoAudio(const QString& executable, const QString& videoPath,
    qint64 videoDurationUs, qint64 musicDurationUs, int ratePercent, int sampleRate,
    qint64 totalSamples, const QString& temporaryDirectory, QString& musicPcmPath,
    const std::atomic_bool* canceled, QString* error);
}
