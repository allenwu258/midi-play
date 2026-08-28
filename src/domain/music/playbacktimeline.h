#pragma once

#include "musicdocument.h"

#include <QVector>

#include <memory>

namespace midi_play::music {

// Immutable playback-order projection of a finalized MusicDocument. Repeat
// expansion and segment timing are computed once so realtime consumers never
// rebuild score navigation state or linearly scan playback segments.
class PlaybackTimeline final {
public:
    explicit PlaybackTimeline(std::shared_ptr<const MusicDocument> document);

    const QVector<PlaybackSegment>& segments() const { return m_segments; }
    Tick durationTicks() const { return m_durationTicks; }
    qint64 durationUs() const { return m_durationUs; }

    qint64 sourceTickToMicroseconds(Tick sourceTick) const;
    qint64 outputTickToMicroseconds(Tick outputTick) const;

private:
    struct SegmentIndex {
        Tick sourceStartTick = 0;
        Tick outputStartTick = 0;
        Tick outputEndTick = 0;
        qint64 outputStartUs = 0;
        qint64 sourceStartUs = 0;
    };

    std::shared_ptr<const MusicDocument> m_document;
    QVector<PlaybackSegment> m_segments;
    QVector<SegmentIndex> m_segmentIndex;
    Tick m_durationTicks = 0;
    qint64 m_durationUs = 0;
};

} // namespace midi_play::music
