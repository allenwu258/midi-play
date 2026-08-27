#pragma once

#include <QString>
#include <QVector>
#include <cstdint>

namespace midi_play::music {

using Tick = std::int64_t;

struct TempoChange {
    Tick tick = 0;
    double bpm = 120.0;
};

struct NoteEvent {
    Tick start = 0;
    Tick duration = 0;
    int pitch = 60;
    int velocity = 90;
    int channel = 0;
    int program = 0;
    int voice = 1;
    int staff = 1;
    bool rest = false;
    bool grace = false;
    bool tieStart = false;
    bool tieStop = false;
    bool staccato = false;
    bool accent = false;
    bool tenuto = false;
    bool ghost = false;
};

struct Measure {
    int number = 0;
    Tick start = 0;
    Tick duration = 0;
    bool repeatStart = false;
    bool repeatEnd = false;
    int repeatCount = 2;
    int endingNumber = 0;
    QString endingType;
};

struct InstrumentChange {
    Tick tick = 0;
    int channel = 0;
    int program = 0;
    QString sourceId;
};

struct PlaybackSegment {
    Tick sourceStart = 0;
    Tick sourceEnd = 0;
    Tick outputStart = 0;
};

struct Track {
    QString id;
    QString name;
    int channel = 0;
    int program = 0;
    bool percussion = false;
    QVector<NoteEvent> notes;
    QVector<Measure> measures;
    QVector<InstrumentChange> instrumentChanges;
};

class MusicDocument {
public:
    static constexpr int kPpq = 480;

    bool isValid() const { return !m_tracks.isEmpty() && m_duration > 0; }
    const QVector<Track>& tracks() const { return m_tracks; }
    QVector<Track>& tracks() { return m_tracks; }
    const QVector<TempoChange>& tempos() const { return m_tempos; }
    QVector<TempoChange>& tempos() { return m_tempos; }
    Tick duration() const { return m_duration; }
    void setDuration(Tick value) { m_duration = value; }
    QString title() const { return m_title; }
    void setTitle(const QString& value) { m_title = value; }

    qint64 tickToMicroseconds(Tick tick) const;
    Tick microsecondsToTick(qint64 microseconds) const;
    QVector<PlaybackSegment> playbackSegments() const;
    Tick playbackDuration() const;

private:
    QVector<Track> m_tracks;
    QVector<TempoChange> m_tempos;
    Tick m_duration = 0;
    QString m_title;
};

} // namespace midi_play::music
