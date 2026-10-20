#pragma once
#include <QtGlobal>
#include <algorithm>

namespace midi_play::visualization {
// At the terminal endpoint sample the left limit: an exact loop boundary freezes
// the final frame instead of flashing frame zero. Music time is the only clock.
inline qint64 backgroundVideoPosition(qint64 musicUs, qint64 musicDurationUs,
                                      qint64 videoDurationUs) noexcept
{
    if (videoDurationUs <= 0) return 0;
    qint64 position = std::max<qint64>(0, musicUs);
    if (musicDurationUs > 0 && position >= musicDurationUs)
        position = musicDurationUs - 1;
    return position % videoDurationUs;
}
}
