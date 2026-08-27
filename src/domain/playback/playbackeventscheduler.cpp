#include "playbackeventscheduler.h"

namespace midi_play::playback {

void PlaybackEventScheduler::setTracks(const QVector<PlaybackData>* tracks)
{
    m_tracks = tracks;
    reset();
}

void PlaybackEventScheduler::reset()
{
    const int size = m_tracks ? m_tracks->size() : 0;
    m_mainCursors.fill(0, size);
    m_offCursors.fill(0, size);
}

void PlaybackEventScheduler::seek(qint64 timestampUs)
{
    const int size = m_tracks ? m_tracks->size() : 0;
    m_mainCursors.resize(size);
    m_offCursors.resize(size);
    for (int index = 0; index < size; ++index) {
        const auto& track = m_tracks->at(index);
        m_mainCursors[index] = track.mainStream.lowerBound(timestampUs);
        m_offCursors[index] = track.offStream.lowerBound(timestampUs);
    }
}

void PlaybackEventScheduler::dispatch(qint64 timestampUs, const EventCallback& callback)
{
    if (!m_tracks || !callback) return;

    for (int index = 0; index < m_tracks->size(); ++index) {
        const auto& track = m_tracks->at(index);
        int& offCursor = m_offCursors[index];
        while (offCursor < track.offStream.size()
               && track.offStream.at(offCursor).timestampUs <= timestampUs) {
            callback(track.offStream.at(offCursor++));
        }
        int& mainCursor = m_mainCursors[index];
        while (mainCursor < track.mainStream.size()
               && track.mainStream.at(mainCursor).timestampUs <= timestampUs) {
            callback(track.mainStream.at(mainCursor++));
        }
    }
}

void PlaybackEventScheduler::dispatchUntil(qint64 timestampUs, const EventCallback& callback)
{
    dispatch(timestampUs, callback);
}

} // namespace midi_play::playback
