#pragma once

#include "domain/music/musicdocument.h"
#include "domain/music/playbacktimeline.h"

#include <QVector>
#include <memory>
#include <optional>

namespace midi_play::playback {

enum class MetronomeAccent {
    Beat,
    Measure
};

struct MetronomeBeat {
    qint64 timeUs = 0;
    quint64 occurrenceId = 0;
    int measureIndex = -1;
    int beatIndex = 0;
    MetronomeAccent accent = MetronomeAccent::Beat;
};

// Immutable metronome occurrences in playback order. Construction is kept
// off the realtime path and uses the same expanded timeline as MIDI events.
class MetronomeTimeline final {
public:
    explicit MetronomeTimeline(std::shared_ptr<const music::MusicDocument> document,
                               std::shared_ptr<const music::PlaybackTimeline> timeline);

    const QVector<MetronomeBeat>& beats() const { return m_beats; }
    int lowerBound(qint64 timeUs) const;
    bool available() const { return m_unavailableReason.isEmpty() && !m_beats.isEmpty(); }
    const QString& unavailableReason() const { return m_unavailableReason; }

private:
    QVector<MetronomeBeat> m_beats;
    QString m_unavailableReason;
};

// Transport cursor independent of UI refresh and audio queue delivery.
// Consume a missed range after stalls, emitting at most one timely click.
class MetronomeScheduler final {
public:
    void seek(const MetronomeTimeline& timeline, qint64 timeUs);
    std::optional<MetronomeBeat> takeDue(const MetronomeTimeline& timeline,
                                       qint64 timeUs, double rate);
private:
    int m_cursor = 0;
};

} // namespace midi_play::playback
