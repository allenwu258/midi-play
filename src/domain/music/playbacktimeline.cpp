#include "playbacktimeline.h"

#include <algorithm>

namespace midi_play::music {

PlaybackTimeline::PlaybackTimeline(std::shared_ptr<const MusicDocument> document)
    : m_document(std::move(document))
{
    if (!m_document) {
        return;
    }

    // Complete the document's lazy tempo index before the immutable timeline
    // can be shared by playback-thread readers.
    m_document->rebuildTempoMap();
    m_segments = m_document->playbackSegments();
    m_segmentIndex.reserve(m_segments.size());

    qint64 elapsedUs = 0;
    for (const auto& segment : m_segments) {
        const Tick length = std::max<Tick>(0, segment.sourceEnd - segment.sourceStart);
        if (length <= 0) {
            continue;
        }

        const qint64 sourceStartUs = m_document->tickToMicroseconds(segment.sourceStart);
        const qint64 sourceEndUs = m_document->tickToMicroseconds(segment.sourceEnd);
        m_segmentIndex.push_back({segment.sourceStart,
                                  segment.outputStart,
                                  segment.outputStart + length,
                                  elapsedUs,
                                  sourceStartUs});
        elapsedUs += std::max<qint64>(0, sourceEndUs - sourceStartUs);
        m_durationTicks = std::max(m_durationTicks, segment.outputStart + length);
    }
    m_durationUs = elapsedUs;
}

qint64 PlaybackTimeline::sourceTickToMicroseconds(Tick sourceTick) const
{
    return m_document ? m_document->tickToMicroseconds(sourceTick) : 0;
}

qint64 PlaybackTimeline::outputTickToMicroseconds(Tick outputTick) const
{
    if (!m_document || m_segmentIndex.isEmpty() || outputTick <= 0) {
        return 0;
    }
    if (outputTick >= m_durationTicks) {
        return m_durationUs;
    }

    const auto it = std::lower_bound(
        m_segmentIndex.cbegin(), m_segmentIndex.cend(), outputTick,
        [](const SegmentIndex& segment, Tick tick) {
            return segment.outputEndTick <= tick;
        });
    if (it == m_segmentIndex.cend()) {
        return m_durationUs;
    }

    const Tick offset = std::clamp<Tick>(outputTick - it->outputStartTick,
                                         0, it->outputEndTick - it->outputStartTick);
    const Tick sourceTick = it->sourceStartTick + offset;
    return it->outputStartUs
         + (m_document->tickToMicroseconds(sourceTick) - it->sourceStartUs);
}

} // namespace midi_play::music
