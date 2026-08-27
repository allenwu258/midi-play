#pragma once

#include "playbacktypes.h"
#include "itransporteventsdispatcher.h"

#include <functional>

namespace midi_play::playback {

class PlaybackEventScheduler final : public ITransportEventsDispatcher {
public:
    using EventCallback = std::function<void(const PlaybackEvent&)>;

    void setTracks(const QVector<PlaybackData>* tracks) override;
    void reset() override;
    void seek(qint64 timestampUs) override;
    void setGeneration(quint64 generation) override { m_generation = generation; }
    void dispatch(qint64 timestampUs, const EventCallback& callback);
    void dispatchUntil(qint64 timestampUs, const EventCallback& callback);
    QVector<PlaybackEvent> collectUntil(qint64 timestampUs);
    PlaybackEventWindow collectWindow(qint64 startUs, qint64 endUs,
                                      quint64 generation) override;

private:
    const QVector<PlaybackData>* m_tracks = nullptr;
    PlaybackEventMap m_mainStream;
    PlaybackEventMap m_offStream;
    int m_mainCursor = 0;
    int m_offCursor = 0;
    quint64 m_generation = 0;
};

} // namespace midi_play::playback
