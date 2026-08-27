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

private:
    const QVector<PlaybackData>* m_tracks = nullptr;
    QVector<int> m_mainCursors;
    QVector<int> m_offCursors;
};

} // namespace midi_play::playback
