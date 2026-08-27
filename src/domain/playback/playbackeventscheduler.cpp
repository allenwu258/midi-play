#include "playbackeventscheduler.h"

namespace midi_play::playback {

void PlaybackEventScheduler::setTracks(const QVector<PlaybackData>* tracks)
{
    m_tracks = tracks;
    m_mainStream.clear();
    m_offStream.clear();
    if (m_tracks) {
        quint64 sequence = 0;
        for (const auto& track : *m_tracks) {
            for (const auto& event : track.mainStream.events()) {
                auto copy = event;
                copy.sequence = sequence++;
                m_mainStream.insert(std::move(copy));
            }
            for (const auto& event : track.offStream.events()) {
                auto copy = event;
                copy.sequence = sequence++;
                m_offStream.insert(std::move(copy));
            }
        }
    }
    m_mainStream.finalize();
    m_offStream.finalize();
    reset();
}

void PlaybackEventScheduler::reset()
{
    m_mainCursor = 0;
    m_offCursor = 0;
}

void PlaybackEventScheduler::seek(qint64 timestampUs)
{
    m_mainCursor = m_mainStream.lowerBound(timestampUs);
    m_offCursor = m_offStream.lowerBound(timestampUs);
}

void PlaybackEventScheduler::dispatch(qint64 timestampUs, const EventCallback& callback)
{
    if (!callback) return;
    const auto events = collectUntil(timestampUs);
    for (const auto& event : events) {
        callback(event);
    }
}

void PlaybackEventScheduler::dispatchUntil(qint64 timestampUs, const EventCallback& callback)
{
    dispatch(timestampUs, callback);
}

QVector<PlaybackEvent> PlaybackEventScheduler::collectUntil(qint64 timestampUs)
{
    QVector<PlaybackEvent> result;
    while (m_offCursor < m_offStream.size()
           && m_offStream.at(m_offCursor).timestampUs <= timestampUs) {
        result.push_back(m_offStream.at(m_offCursor++));
    }
    while (m_mainCursor < m_mainStream.size()
           && m_mainStream.at(m_mainCursor).timestampUs <= timestampUs) {
        result.push_back(m_mainStream.at(m_mainCursor++));
    }
    std::stable_sort(result.begin(), result.end(), [](const auto& left, const auto& right) {
        if (left.timestampUs != right.timestampUs) return left.timestampUs < right.timestampUs;
        const int leftPriority = playbackEventPriority(left);
        const int rightPriority = playbackEventPriority(right);
        if (leftPriority != rightPriority) return leftPriority < rightPriority;
        return left.sequence < right.sequence;
    });
    return result;
}

} // namespace midi_play::playback
