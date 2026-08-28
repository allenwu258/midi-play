#pragma once

#include "playbacktypes.h"

#include <QString>

#include <atomic>
#include <memory>

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

// Atomic handoff owned by an AudioDevice backend and shared with its
// scheduler-facing decorator. The backend publishes from its audio callback
// or worker thread; realtime consumers only perform an atomic load.
class PlaybackClockSnapshot final {
public:
    void publish(qint64 positionUs) noexcept
    {
        m_positionUs.store(positionUs, std::memory_order_release);
    }

    void invalidate() noexcept { publish(-1); }

    qint64 positionUs() const noexcept
    {
        return m_positionUs.load(std::memory_order_acquire);
    }

private:
    std::atomic<qint64> m_positionUs {-1};
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
    // Direct services may resolve their clock on the caller's thread. A
    // threaded decorator must use clockSnapshot() and must not call this
    // virtual function across thread boundaries.
    virtual qint64 clockPositionUs() const { return -1; }
    // AudioDevice backends create this snapshot before they are wrapped and
    // continuously publish their frame-derived position into it. Returning an
    // empty snapshot makes a threaded decorator fall back to the software
    // clock instead of performing an unsafe cross-thread call.
    virtual std::shared_ptr<const PlaybackClockSnapshot> clockSnapshot() const { return {}; }
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
