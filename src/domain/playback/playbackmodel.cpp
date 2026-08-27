#include "playbackmodel.h"

#include <algorithm>

namespace midi_play::playback {

namespace {

void buildStateSnapshots(PlaybackData& data)
{
    QVector<ChannelState> channels(16);
    QHash<int, QVector<ActiveNoteState>> active;
    qint64 nextSnapshotUs = 0;
    int processedEvents = 0;
    constexpr qint64 snapshotIntervalUs = 1'000'000;

    auto appendSnapshot = [&](qint64 timestampUs) {
        PlaybackStateSnapshot snapshot;
        snapshot.timestampUs = timestampUs;
        snapshot.eventIndex = processedEvents;
        for (int channel = 0; channel < channels.size(); ++channel) {
            snapshot.channels.insert(channel, channels[channel]);
        }
        for (auto it = active.cbegin(); it != active.cend(); ++it) {
            snapshot.activeNotes += it.value();
        }
        data.snapshots.push_back(std::move(snapshot));
    };

    for (const auto& event : data.events) {
        while (event.timestampUs >= nextSnapshotUs) {
            appendSnapshot(nextSnapshotUs);
            nextSnapshotUs += snapshotIntervalUs;
        }
        const int channel = std::clamp(event.channel, 0, 15);
        if (event.kind == PlaybackEventKind::ProgramChange) {
            channels[channel].initialized = true;
            channels[channel].program = event.program;
        } else if (event.kind == PlaybackEventKind::ControlChange) {
            channels[channel].initialized = true;
            channels[channel].controllers.insert(event.controller, event.value);
        } else if (event.kind == PlaybackEventKind::PitchBend) {
            channels[channel].initialized = true;
            channels[channel].pitchBend = event.value;
        } else if (event.kind == PlaybackEventKind::ChannelPressure) {
            channels[channel].initialized = true;
            channels[channel].channelPressure = event.value;
        } else if (event.kind == PlaybackEventKind::NoteOn) {
            active[channel * 128 + event.pitch].push_back({channel, event.pitch, event.velocity,
                                                            event.timestampUs + event.durationUs});
        } else if (event.kind == PlaybackEventKind::NoteOff) {
            auto it = active.find(channel * 128 + event.pitch);
            if (it != active.end() && !it->isEmpty()) {
                it->removeLast();
                if (it->isEmpty()) active.erase(it);
            }
        }
        ++processedEvents;
    }
    if (data.snapshots.isEmpty() || data.snapshots.back().timestampUs < nextSnapshotUs) {
        appendSnapshot(nextSnapshotUs);
    }
}

} // namespace

PlaybackModel::PlaybackModel(std::shared_ptr<const music::MusicDocument> document)
    : m_document(std::move(document))
{
    if (!m_document) return;
    m_context = std::make_shared<PlaybackContext>(m_document);
    PlaybackEventsRenderer renderer(m_context);
    const auto segments = m_document->playbackSegments();
    for (const auto& track : m_document->tracks()) {
        QMap<QString, QVector<const music::NoteEvent*>> voiceGroups;
        for (const auto& note : track.notes) {
            const QString key = QStringLiteral("%1/staff%2/voice%3").arg(track.id).arg(note.staff).arg(note.voice);
            voiceGroups[key].push_back(&note);
        }
        for (auto group = voiceGroups.cbegin(); group != voiceGroups.cend(); ++group) {
            PlaybackData data;
            data.trackId = group.key();
            data.setupData.soundId = track.program == 0 ? QStringLiteral("piano") : QStringLiteral("midi-program-%1").arg(track.program);
        for (const auto& segment : segments) {
            for (const auto* note : group.value()) {
                if (note->start < segment.sourceStart || note->start >= segment.sourceEnd) continue;
                const music::Tick outputTick = segment.outputStart + (note->start - segment.sourceStart);
                const music::Tick segmentDuration = std::max<music::Tick>(1,
                    std::min(note->duration, segment.sourceEnd - note->start));
                const qint64 timestamp = m_document->playbackTickToMicroseconds(outputTick);
                const qint64 end = m_document->playbackTickToMicroseconds(outputTick + segmentDuration);
                PlaybackEvent event;
                event.timestampUs = timestamp;
                event.durationUs = qMax<qint64>(1'000, end - timestamp);
                event.channel = note->channel;
                event.pitch = note->pitch;
                event.velocity = note->velocity;
                event.program = note->program;
                event.tieStart = note->tieStart;
                event.tieStop = note->tieStop;
                event.staccato = note->staccato;
                event.accent = note->accent;
                event.tenuto = note->tenuto;
                event.ghost = note->ghost;
                event.voice = note->voice;
                event.staff = note->staff;
                event.marcato = note->marcato;
                event.tremolo = note->tremolo;
                data.events.push_back(event);
            }
            for (const auto& change : track.instrumentChanges) {
                if (change.tick < segment.sourceStart || change.tick >= segment.sourceEnd) continue;
                const music::Tick outputTick = segment.outputStart + (change.tick - segment.sourceStart);
                data.events.push_back({m_document->playbackTickToMicroseconds(outputTick), 0, change.channel, 0, 0,
                                       change.program, false, false, false, false, false, false,
                                       PlaybackEventKind::ProgramChange});
            }
            for (const auto& change : track.controlChanges) {
                if (change.tick < segment.sourceStart || change.tick >= segment.sourceEnd) continue;
                const music::Tick outputTick = segment.outputStart + (change.tick - segment.sourceStart);
                PlaybackEvent event;
                event.timestampUs = m_document->playbackTickToMicroseconds(outputTick);
                event.channel = change.channel;
                event.controller = change.controller;
                event.value = change.value;
                event.kind = PlaybackEventKind::ControlChange;
                data.events.push_back(event);
            }
            for (const auto& change : track.pitchBendChanges) {
                if (change.tick < segment.sourceStart || change.tick >= segment.sourceEnd) continue;
                const music::Tick outputTick = segment.outputStart + (change.tick - segment.sourceStart);
                PlaybackEvent event;
                event.timestampUs = m_document->playbackTickToMicroseconds(outputTick);
                event.channel = change.channel;
                event.value = change.value;
                event.kind = PlaybackEventKind::PitchBend;
                data.events.push_back(event);
            }
            for (const auto& change : track.channelPressureChanges) {
                if (change.tick < segment.sourceStart || change.tick >= segment.sourceEnd) continue;
                const music::Tick outputTick = segment.outputStart + (change.tick - segment.sourceStart);
                PlaybackEvent event;
                event.timestampUs = m_document->playbackTickToMicroseconds(outputTick);
                event.channel = change.channel;
                event.value = change.value;
                event.kind = PlaybackEventKind::ChannelPressure;
                data.events.push_back(event);
            }
        }
        data.events = renderer.render(data);

        // Keep note-off events in the same timestamp-ordered stream as note-on
        // and controller events. The audio backend must receive explicit note-off
        // commands; scheduling them with a UI timer introduces unbounded jitter.
        QVector<PlaybackEvent> noteOffEvents;
        noteOffEvents.reserve(data.events.size());
        for (const auto& event : data.events) {
            if (!event.isNoteOn() || event.durationUs <= 0) {
                continue;
            }
            auto noteOff = event;
            noteOff.timestampUs = event.timestampUs + event.durationUs;
            noteOff.durationUs = 0;
            noteOff.kind = PlaybackEventKind::NoteOff;
            noteOff.sequence = event.sequence + 1;
            noteOffEvents.push_back(noteOff);
        }
        data.offEvents = noteOffEvents;
        data.mainStream.assign(data.events);
        data.mainStream.finalize();
        data.offStream.assign(data.offEvents);
        data.offStream.finalize();
        data.events += noteOffEvents;
        std::sort(data.events.begin(), data.events.end(), [](const auto& left, const auto& right) {
            if (left.timestampUs != right.timestampUs) {
                return left.timestampUs < right.timestampUs;
            }
            if (left.kind != right.kind) {
                // At an identical timestamp, release a previous note before
                // starting a new one on the same pitch/channel.
                return left.kind == PlaybackEventKind::NoteOff;
            }
            return left.sequence < right.sequence;
        });
        data.index = std::make_shared<PlaybackEventIndex>(data.events);
        data.offIndex = std::make_shared<PlaybackEventIndex>(data.offEvents);
        buildStateSnapshots(data);
        m_tracks.push_back(std::move(data));
        }
    }
}

qint64 PlaybackModel::durationUs() const
{
    return m_document ? m_document->playbackTickToMicroseconds(m_document->playbackDuration()) : 0;
}

const PlaybackData* PlaybackModel::track(const QString& id) const
{
    for (const auto& track : m_tracks) if (track.trackId == id) return &track;
    return nullptr;
}

} // namespace midi_play::playback
