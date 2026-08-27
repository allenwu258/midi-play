#pragma once

#include "playbacktypes.h"

#include <functional>

namespace midi_play::playback {

class PlaybackEventScheduler final {
public:
    using EventCallback = std::function<void(const PlaybackEvent&)>;

    void setTracks(const QVector<PlaybackData>* tracks);
    void reset();
    void seek(qint64 timestampUs);
    void dispatch(qint64 timestampUs, const EventCallback& callback);
    void dispatchUntil(qint64 timestampUs, const EventCallback& callback);
    QVector<PlaybackEvent> collectUntil(qint64 timestampUs);

private:
    const QVector<PlaybackData>* m_tracks = nullptr;
    PlaybackEventMap m_mainStream;
    PlaybackEventMap m_offStream;
    int m_mainCursor = 0;
    int m_offCursor = 0;
};

} // namespace midi_play::playback
