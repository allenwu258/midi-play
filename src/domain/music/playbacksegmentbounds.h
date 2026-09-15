#pragma once

#include "musicdocument.h"

namespace midi_play::music {

// A bar boundary is not a note release. Only a discontinuity in the expanded
// playback path (repeat/navigation/ending) limits a sustained event.
inline QVector<Tick> continuousPlaybackEnds(const QVector<PlaybackSegment>& segments)
{
    QVector<Tick> ends(segments.size());
    for (qsizetype i = segments.size(); i-- > 0;) {
        const auto& segment = segments[i];
        ends[i] = segment.sourceEnd;
        if (i + 1 < segments.size()
            && segment.sourceEnd == segments[i + 1].sourceStart
            && segment.outputStart + segment.sourceEnd - segment.sourceStart == segments[i + 1].outputStart)
            ends[i] = ends[i + 1];
    }
    return ends;
}

} // namespace midi_play::music
