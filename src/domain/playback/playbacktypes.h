#pragma once

#include "domain/music/musicdocument.h"

#include <QString>
#include <QHash>
#include <QVector>
#include <memory>
#include <algorithm>
#include <utility>

namespace midi_play::playback {

enum class State { Empty, Ready, Playing, Paused, Stopped, Error };

struct AudioResource {
    QString path;
    int soundFontId = -1;
};

struct AudioResourceMeta {
    QString id;
    QString vendor;
    QString name;
    QString type = QStringLiteral("soundfont");
    QString path;
    QHash<QString, QString> attributes;
};

struct SoundPreset {
    QString id;
    QString name;
    int bank = 0;
    int program = 0;
};

struct PlaybackSetupData {
    QString soundId = QStringLiteral("piano");
    QString musicXmlSoundId;
    bool supportsSingleNoteDynamics = false;
};

enum class PlaybackEventKind {
    NoteOn,
    NoteOff,
    ProgramChange,
    ControlChange,
    PitchBend,
    ChannelPressure,
    AllNotesOff
};

enum class ArticulationType {
    Staccato,
    Accent,
    Tenuto,
    Ghost,
    Marcato,
    Tremolo
};

struct ArticulationAppliedData {
    ArticulationType type = ArticulationType::Tenuto;
    double durationFactor = 1.0;
    double velocityFactor = 1.0;
    int velocityOffset = 0;
};

struct ExpressionCurvePoint {
    qint64 offsetUs = 0;
    float value = 0.0F;
};

struct NoteExpressionContext {
    QVector<ArticulationAppliedData> articulations;
    QVector<ExpressionCurvePoint> pitchCurve;
    QVector<ExpressionCurvePoint> expressionCurve;
};

struct PlaybackEvent {
    qint64 timestampUs = 0;
    qint64 durationUs = 0;
    int channel = 0;
    int pitch = 60;
    int velocity = 90;
    int program = 0;
    bool tieStart = false;
    bool tieStop = false;
    bool staccato = false;
    bool accent = false;
    bool tenuto = false;
    bool ghost = false;
    PlaybackEventKind kind = PlaybackEventKind::NoteOn;
    quint64 sequence = 0;
    int controller = 0;
    int value = 0;
    int voice = 1;
    int staff = 1;
    bool marcato = false;
    bool tremolo = false;
    NoteExpressionContext expression;

    bool isNoteOn() const { return kind == PlaybackEventKind::NoteOn; }
    bool isNoteOff() const { return kind == PlaybackEventKind::NoteOff; }
};

class PlaybackEventMap final {
public:
    void clear()
    {
        m_events.clear();
        m_timestamps.clear();
    }

    void assign(const QVector<PlaybackEvent>& events)
    {
        m_events = events;
        m_timestamps.clear();
        m_timestamps.reserve(m_events.size());
        for (const auto& event : m_events) {
            m_timestamps.push_back(event.timestampUs);
        }
    }

    void insert(PlaybackEvent event)
    {
        m_events.push_back(std::move(event));
    }

    void finalize()
    {
        std::sort(m_events.begin(), m_events.end(), [](const auto& left, const auto& right) {
            if (left.timestampUs != right.timestampUs) return left.timestampUs < right.timestampUs;
            if (left.kind != right.kind) return left.kind == PlaybackEventKind::NoteOff;
            return left.sequence < right.sequence;
        });
        m_timestamps.clear();
        m_timestamps.reserve(m_events.size());
        for (const auto& event : m_events) m_timestamps.push_back(event.timestampUs);
    }

    const QVector<PlaybackEvent>& events() const { return m_events; }
    bool isEmpty() const { return m_events.isEmpty(); }
    int size() const { return m_events.size(); }
    const PlaybackEvent& at(int index) const { return m_events.at(index); }

    int lowerBound(qint64 timestampUs) const
    {
        return static_cast<int>(std::lower_bound(m_timestamps.cbegin(), m_timestamps.cend(), timestampUs)
                                - m_timestamps.cbegin());
    }

private:
    QVector<PlaybackEvent> m_events;
    QVector<qint64> m_timestamps;
};

struct ChannelState {
    bool initialized = false;
    int program = 0;
    int bankMsb = 0;
    int bankLsb = 0;
    int pitchBend = 8192;
    int channelPressure = 0;
    QHash<int, int> controllers;
};

struct ActiveNoteState {
    int channel = 0;
    int pitch = 60;
    int velocity = 90;
    qint64 endTimestampUs = 0;
};

struct PlaybackStateSnapshot {
    qint64 timestampUs = 0;
    int eventIndex = 0;
    QHash<int, ChannelState> channels;
    QVector<ActiveNoteState> activeNotes;
};

class PlaybackEventIndex final {
public:
    explicit PlaybackEventIndex(const QVector<PlaybackEvent>& events)
    {
        m_timestamps.reserve(events.size());
        for (const auto& event : events) m_timestamps.push_back(event.timestampUs);
    }

    int lowerBound(qint64 timestampUs) const
    {
        return static_cast<int>(std::lower_bound(m_timestamps.cbegin(), m_timestamps.cend(), timestampUs) - m_timestamps.cbegin());
    }

private:
    QVector<qint64> m_timestamps;
};

struct PlaybackData {
    QString trackId;
    PlaybackSetupData setupData;
    QVector<PlaybackEvent> events;
    QVector<PlaybackEvent> offEvents;
    PlaybackEventMap mainStream;
    PlaybackEventMap offStream;
    QVector<PlaybackStateSnapshot> snapshots;
    std::shared_ptr<const PlaybackEventIndex> index;
    std::shared_ptr<const PlaybackEventIndex> offIndex;
};

} // namespace midi_play::playback
