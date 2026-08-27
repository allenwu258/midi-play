#include "playbackmodel.h"

#include <algorithm>

namespace midi_play::playback {

namespace {

void buildStateSnapshots(const QVector<PlaybackEvent>& events,
                         QVector<PlaybackStateSnapshot>& snapshots)
{
    QVector<ChannelState> channels(16);
    QHash<int, QVector<ActiveNoteState>> active;
    QVector<QVector<qint64>> sustainReleaseTimes(16);
    QVector<QVector<qint64>> sostenutoReleaseTimes(16);
    snapshots.clear();
    for (const auto& event : events) {
        if (event.kind == PlaybackEventKind::ControlChange
            && (event.controller == 64 || event.controller == 66) && event.value < 64
            && event.channel >= 0 && event.channel < sustainReleaseTimes.size()) {
            auto& releaseTimes = event.controller == 64
                ? sustainReleaseTimes[event.channel]
                : sostenutoReleaseTimes[event.channel];
            releaseTimes.push_back(event.timestampUs);
        }
    }
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
        snapshots.push_back(std::move(snapshot));
    };

    for (const auto& event : events) {
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
            if (event.controller == 0) {
                channels[channel].bankMsb = std::clamp(event.value, 0, 127);
            } else if (event.controller == 32) {
                channels[channel].bankLsb = std::clamp(event.value, 0, 127);
            }
            switch (event.controller) {
            case 101: channels[channel].rpnMsb = std::clamp(event.value, 0, 127); break;
            case 100: channels[channel].rpnLsb = std::clamp(event.value, 0, 127); break;
            case 99: channels[channel].nrpnMsb = std::clamp(event.value, 0, 127); break;
            case 98: channels[channel].nrpnLsb = std::clamp(event.value, 0, 127); break;
            case 6:
                channels[channel].dataEntryMsb = std::clamp(event.value, 0, 127);
                if (channels[channel].rpnMsb == 0 && channels[channel].rpnLsb == 0) {
                    channels[channel].pitchBendRangeSemitones = channels[channel].dataEntryMsb;
                }
                break;
            case 38:
                channels[channel].dataEntryLsb = std::clamp(event.value, 0, 127);
                if (channels[channel].rpnMsb == 0 && channels[channel].rpnLsb == 0) {
                    channels[channel].pitchBendRangeCents = channels[channel].dataEntryLsb;
                }
                break;
            default: break;
            }
            if (event.controller == 64 && event.value < 64) {
                auto activeIt = active.begin();
                while (activeIt != active.end()) {
                    auto& notes = activeIt.value();
                    for (auto& note : notes) note.sustainLatched = false;
                    notes.erase(std::remove_if(notes.begin(), notes.end(), [](const auto& note) {
                        return note.keyReleased && !note.sostenutoLatched;
                    }), notes.end());
                    if (notes.isEmpty()) activeIt = active.erase(activeIt);
                    else ++activeIt;
                }
            }
            if (event.controller == 66 && event.value >= 64) {
                for (auto activeIt = active.begin(); activeIt != active.end(); ++activeIt) {
                    for (auto& note : activeIt.value()) {
                        if (!note.keyReleased) note.sostenutoCaptured = true;
                    }
                }
            } else if (event.controller == 66 && event.value < 64) {
                auto activeIt = active.begin();
                while (activeIt != active.end()) {
                    auto& notes = activeIt.value();
                    for (auto& note : notes) {
                        if (note.sostenutoCaptured) note.sostenutoLatched = false;
                    }
                    notes.erase(std::remove_if(notes.begin(), notes.end(), [](const auto& note) {
                        return note.keyReleased && !note.sustainLatched;
                    }), notes.end());
                    if (notes.isEmpty()) activeIt = active.erase(activeIt);
                    else ++activeIt;
                }
            }
        } else if (event.kind == PlaybackEventKind::PitchBend) {
            channels[channel].initialized = true;
            channels[channel].pitchBend = event.value;
        } else if (event.kind == PlaybackEventKind::ChannelPressure) {
            channels[channel].initialized = true;
            channels[channel].channelPressure = event.value;
        } else if (event.kind == PlaybackEventKind::PolyPressure) {
            channels[channel].initialized = true;
            channels[channel].polyPressure.insert(event.pitch, event.value);
        } else if (event.kind == PlaybackEventKind::NoteOn) {
            active[channel * 128 + event.pitch].push_back({channel, event.pitch, event.velocity,
                                                            event.timestampUs + event.durationUs});
        } else if (event.kind == PlaybackEventKind::NoteOff) {
            auto it = active.find(channel * 128 + event.pitch);
            if (it != active.end() && !it->isEmpty()) {
                auto& notes = it.value();
                auto& note = notes.last();
                const bool sustainDown = channels[channel].controllers.value(64, 0) >= 64;
                const bool sostenutoDown = channels[channel].controllers.value(66, 0) >= 64
                    && note.sostenutoCaptured;
                if (sustainDown || sostenutoDown) {
                    note.keyReleased = true;
                    note.sustainLatched = sustainDown;
                    note.sostenutoLatched = sostenutoDown;
                    note.endTimestampUs = event.timestampUs;
                    if (sustainDown) {
                        const auto& releases = sustainReleaseTimes[channel];
                        const auto releaseIt = std::lower_bound(releases.cbegin(), releases.cend(), event.timestampUs);
                        if (releaseIt != releases.cend()) note.endTimestampUs = qMax(note.endTimestampUs, *releaseIt);
                    }
                    if (sostenutoDown) {
                        const auto& releases = sostenutoReleaseTimes[channel];
                        const auto releaseIt = std::lower_bound(releases.cbegin(), releases.cend(), event.timestampUs);
                        if (releaseIt != releases.cend()) note.endTimestampUs = qMax(note.endTimestampUs, *releaseIt);
                    }
                } else {
                    notes.removeLast();
                    if (notes.isEmpty()) active.erase(it);
                }
            }
        }
        ++processedEvents;
    }
    if (snapshots.isEmpty() || snapshots.back().timestampUs < nextSnapshotUs) {
        appendSnapshot(nextSnapshotUs);
    }
}

} // namespace

PlaybackModel::PlaybackModel(std::shared_ptr<const music::MusicDocument> document)
    : m_document(std::move(document))
{
    if (!m_document) return;
    m_scoreDom = music::ScorePlaybackDom::build(*m_document);
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
                event.sequence = note->sequence;
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
                PlaybackEvent program;
                program.timestampUs = m_document->playbackTickToMicroseconds(outputTick);
                program.channel = change.channel;
                program.program = change.program;
                program.bankMsb = change.bankMsb;
                program.bankLsb = change.bankLsb;
                program.sequence = change.sequence;
                program.kind = PlaybackEventKind::ProgramChange;
                data.events.push_back(program);
            }
            for (const auto& change : track.controlChanges) {
                if (change.tick < segment.sourceStart || change.tick >= segment.sourceEnd) continue;
                const music::Tick outputTick = segment.outputStart + (change.tick - segment.sourceStart);
                PlaybackEvent event;
                event.timestampUs = m_document->playbackTickToMicroseconds(outputTick);
                event.channel = change.channel;
                event.controller = change.controller;
                event.value = change.value;
                event.sequence = change.sequence;
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
                event.sequence = change.sequence;
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
                event.sequence = change.sequence;
                event.kind = PlaybackEventKind::ChannelPressure;
                data.events.push_back(event);
            }
            for (const auto& change : track.polyPressureChanges) {
                if (change.tick < segment.sourceStart || change.tick >= segment.sourceEnd) continue;
                const music::Tick outputTick = segment.outputStart + (change.tick - segment.sourceStart);
                PlaybackEvent event;
                event.timestampUs = m_document->playbackTickToMicroseconds(outputTick);
                event.channel = change.channel;
                event.pitch = change.pitch;
                event.value = change.value;
                event.sequence = change.sequence;
                event.kind = PlaybackEventKind::PolyPressure;
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
            const int leftPriority = playbackEventPriority(left);
            const int rightPriority = playbackEventPriority(right);
            if (leftPriority != rightPriority) return leftPriority < rightPriority;
            return left.sequence < right.sequence;
        });
        data.index = std::make_shared<PlaybackEventIndex>(data.events);
        data.offIndex = std::make_shared<PlaybackEventIndex>(data.offEvents);
        buildStateSnapshots(data.events, data.snapshots);
        m_tracks.push_back(std::move(data));
        }
    }

    // Build one canonical event stream for score-wide state reconstruction.
    // Track-local streams remain the scheduling source, while this stream is
    // the authoritative seek/playback context for shared MIDI channels.
    quint64 globalSequence = 0;
    for (const auto& track : m_tracks) {
        for (const auto& sourceEvent : track.events) {
            auto event = sourceEvent;
            event.sequence = globalSequence++;
            m_globalEvents.push_back(std::move(event));
        }
    }
    std::sort(m_globalEvents.begin(), m_globalEvents.end(), [](const auto& left, const auto& right) {
        if (left.timestampUs != right.timestampUs) return left.timestampUs < right.timestampUs;
        const int leftPriority = playbackEventPriority(left);
        const int rightPriority = playbackEventPriority(right);
        if (leftPriority != rightPriority) return leftPriority < rightPriority;
        return left.sequence < right.sequence;
    });
    m_globalIndex = std::make_shared<PlaybackEventIndex>(m_globalEvents);
    buildStateSnapshots(m_globalEvents, m_globalSnapshots);
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
