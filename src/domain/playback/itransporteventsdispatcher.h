#pragma once

#include "playbacktypes.h"

namespace midi_play::playback {

struct PlaybackEventWindow {
    qint64 startUs = 0;
    qint64 endUs = 0;
    quint64 generation = 0;
    QVector<PlaybackEvent> events;
};

class ITransportEventsDispatcher {
public:
    virtual ~ITransportEventsDispatcher() = default;
    virtual void setTracks(const QVector<PlaybackData>* tracks) = 0;
    virtual void reset() = 0;
    virtual void seek(qint64 timestampUs) = 0;
    virtual void setGeneration(quint64 generation) = 0;
    virtual PlaybackEventWindow collectWindow(qint64 startUs, qint64 endUs,
                                               quint64 generation) = 0;
};

} // namespace midi_play::playback
