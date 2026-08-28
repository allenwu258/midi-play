#pragma once

#include "playbacktypes.h"

#include <QString>

namespace midi_play::playback {

enum class PlaybackClockSource {
    SoftwareMonotonic,
    AudioDevice
};

// Backend capabilities are immutable for the lifetime of a service. Keeping
// them in one snapshot prevents realtime scheduling code from querying an
// audio worker synchronously on every tick.
struct PlaybackBackendCapabilities {
    PlaybackClockSource clockSource = PlaybackClockSource::SoftwareMonotonic;
    bool timedEvents = false;
    bool perNoteExpression = false;

    bool usesAudioClock() const { return clockSource == PlaybackClockSource::AudioDevice; }
};

class IPlaybackAudioService {
public:
    virtual ~IPlaybackAudioService() = default;
    virtual bool loadSoundFont(const QString& path, QString* error) = 0;
    virtual bool configureTrack(const QString& trackId, int channel, int program, QString* error) = 0;
    virtual bool addTrack(const PlaybackData& data, QString* error) = 0;
    virtual bool start() = 0;
    virtual bool pause() = 0;
    virtual bool stop() = 0;
    virtual bool seek(qint64 positionUs) = 0;
    virtual bool setTransportPosition(qint64 positionUs)
    {
        Q_UNUSED(positionUs)
        return true;
    }
    // AudioDevice backends must expose a lock-free or equivalently bounded,
    // thread-safe snapshot. The playback scheduler may read it every tick and
    // must never wait on the realtime audio thread.
    virtual qint64 clockPositionUs() const { return -1; }
    virtual PlaybackClockSource clockSource() const { return PlaybackClockSource::SoftwareMonotonic; }
    virtual bool supportsTimedEvents() const { return false; }
    virtual bool supportsPerNoteExpression() const { return false; }
    virtual PlaybackBackendCapabilities capabilities() const
    {
        return {clockSource(), supportsTimedEvents(), supportsPerNoteExpression()};
    }
    virtual bool flush() = 0;
    virtual void submit(const PlaybackEvent& event) = 0;
    virtual void submitOff(const PlaybackEvent& event) = 0;
    // The default keeps simple backends source-compatible while allowing
    // realtime-capable implementations to amortize cross-thread dispatch.
    virtual void submitBatch(const QVector<PlaybackEvent>& events)
    {
        for (const auto& event : events) {
            submit(event);
        }
    }
    virtual void setEventGeneration(quint64 generation)
    {
        Q_UNUSED(generation)
    }
    virtual void submitBatch(const QVector<PlaybackEvent>& events, quint64 generation)
    {
        Q_UNUSED(generation)
        submitBatch(events);
    }
    virtual bool updateMainStream(const QString& trackId, const PlaybackEventMap& events)
    {
        Q_UNUSED(trackId)
        Q_UNUSED(events)
        return true;
    }
    virtual bool updateOffStream(const QString& trackId, const PlaybackEventMap& events)
    {
        Q_UNUSED(trackId)
        Q_UNUSED(events)
        return true;
    }
};

} // namespace midi_play::playback
