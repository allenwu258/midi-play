#include "playbackmodel.h"
#include "domain/music/playbacksegmentbounds.h"

// 实现轨道事件投影、表达渲染、NoteOff 分流和状态快照构建。
#include <algorithm>

namespace midi_play::playback {

namespace {

// 每秒生成一个快照，记录 MIDI 通道状态和仍在发声的音符。
// 这些快照使 seek 可以从最近检查点恢复，而不必从曲首重放全部事件。
void buildStateSnapshots(const QVector<PlaybackEvent>& events,
                         QVector<PlaybackStateSnapshot>& snapshots)
{
    QVector<ChannelState> channels(16);
    QHash<qint64, QVector<ActiveNoteState>> active;
    QVector<QVector<qint64>> sustainReleaseTimes(16);
    QVector<QVector<qint64>> sostenutoReleaseTimes(16);
    snapshots.clear();
    // 预收集 sustain/sostenuto 释放时刻，NoteOff 处理时可快速找到后续释放点。
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
        // eventIndex 指向尚未应用的首个事件，检查点同时间戳的事件仍留给恢复流程重放。
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
        // 时间戳跨过检查点时先保存当前状态，再应用当前事件。
        while (event.timestampUs >= nextSnapshotUs) {
            appendSnapshot(nextSnapshotUs);
            nextSnapshotUs += snapshotIntervalUs;
        }
        const int channel = std::clamp(event.channel, 0, 15);
        if (event.kind == PlaybackEventKind::ProgramChange) {
            channels[channel].initialized = true;
            channels[channel].program = event.program;
        } else if (event.kind == PlaybackEventKind::ControlChange) {
            // 控制器既要保留原值，也要更新 bank、RPN/NRPN 和踏板派生状态。
            channels[channel].initialized = true;
            channels[channel].controllers.insert(event.controller, event.value);
            if (event.controller == 0) {
                channels[channel].bankMsb = std::clamp(event.value, 0, 127);
            } else if (event.controller == 32) {
                channels[channel].bankLsb = std::clamp(event.value, 0, 127);
            }
            // Data Entry 只有在 RPN 0/0 被选中时更新弯音灵敏度；其他参数仍保留原控制值。
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
                // 松开 sustain 后，已释放且未被 sostenuto 锁存的音符立即结束。
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
                // sostenuto 按下瞬间只捕获当时尚未释放的音符。
                for (auto activeIt = active.begin(); activeIt != active.end(); ++activeIt) {
                    for (auto& note : activeIt.value()) {
                        if (!note.keyReleased) note.sostenutoCaptured = true;
                    }
                }
            } else if (event.controller == 66 && event.value < 64) {
                // 松开 sostenuto 后清理未被 sustain 保持的已释放音符。
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
            // 同一 noteId 允许多个重叠实例，因此按 key 保存栈。
            const qint64 noteKey = event.noteId >= 0 ? event.noteId
                                                     : qint64(channel * 128 + event.pitch);
            ActiveNoteState note;
            note.noteId = event.noteId;
            note.channel = channel;
            note.pitch = event.pitch;
            note.velocity = event.velocity;
            note.endTimestampUs = event.timestampUs + event.durationUs;
            active[noteKey].push_back(note);
        } else if (event.kind == PlaybackEventKind::NoteOff) {
            // NoteOff 可能只释放琴键；踏板按下时音符继续保持活动。
            const qint64 noteKey = event.noteId >= 0 ? event.noteId
                                                     : qint64(channel * 128 + event.pitch);
            auto it = active.find(noteKey);
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
                    // 双踏板同时保持时取各自后续释放位置的最大值，避免低估恢复音符的剩余时长。
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
    // 额外保存最后事件之后的检查点，尾段跳转可直接获得最终通道状态。
    if (snapshots.isEmpty() || snapshots.back().timestampUs < nextSnapshotUs) {
        appendSnapshot(nextSnapshotUs);
    }
}

} // namespace

// 从定稿文档一次性生成逻辑轨道、反复投影、调度事件及 seek 检查点。
PlaybackModel::PlaybackModel(std::shared_ptr<const music::MusicDocument> document)
    : m_document(std::move(document))
{
    // 文档为空时保留空模型；其余派生对象均由定稿文档一次性构建。
    if (!m_document) return;
    m_timeline = std::make_shared<music::PlaybackTimeline>(m_document);
    m_scoreDom = music::ScorePlaybackDom::build(*m_document);
    m_context = std::make_shared<PlaybackContext>(m_document, m_timeline);
    PlaybackEventsRenderer renderer(m_context);
    const auto& segments = m_timeline->segments();
    const auto continuousEnds = music::continuousPlaybackEnds(segments);
    for (const auto& track : m_document->tracks()) {
        // 按 track/staff/voice 拆分，保证同一播放数据组中的和弦关系稳定。
        QMap<QString, QVector<const music::NoteEvent*>> voiceGroups;
        for (const auto& note : track.notes) {
            const QString key = QStringLiteral("%1/staff%2/voice%3").arg(track.id).arg(note.staff).arg(note.voice);
            voiceGroups[key].push_back(&note);
        }
        for (auto group = voiceGroups.cbegin(); group != voiceGroups.cend(); ++group) {
            PlaybackData data;
            data.trackId = group.key();
            data.setupData.soundId = track.program == 0 ? QStringLiteral("piano") : QStringLiteral("midi-program-%1").arg(track.program);
        for (int segmentIndex = 0; segmentIndex < segments.size(); ++segmentIndex) {
            const auto& segment = segments[segmentIndex];
            // 仅投影当前反复段覆盖的源事件；segmentDuration 在不连续边界截断。
            for (const auto* note : group.value()) {
                if (note->start < segment.sourceStart || note->start >= segment.sourceEnd) continue;
                const music::Tick outputTick = segment.outputStart + (note->start - segment.sourceStart);
                const music::Tick segmentDuration = std::max<music::Tick>(1,
                    std::min(note->duration, continuousEnds[segmentIndex] - note->start));
                const qint64 timestamp = m_timeline->outputTickToMicroseconds(outputTick);
                const qint64 end = m_timeline->outputTickToMicroseconds(outputTick + segmentDuration);
                PlaybackEvent event;
                // 事件时间改用输出时间线，音符内容和演奏法沿用音乐域派生字段。
                event.timestampUs = timestamp;
                event.durationUs = qMax<qint64>(1'000, end - timestamp);
                event.channel = note->channel;
                event.pitch = note->pitch;
                event.velocity = note->velocity;
                event.program = note->program;
                event.noteId = static_cast<qint64>(note->noteId);
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
            // 通道状态变化也按反复段重新定位；事件位于半开源区间，边界不会投影两次。
            for (const auto& change : track.instrumentChanges) {
                if (change.tick < segment.sourceStart || change.tick >= segment.sourceEnd) continue;
                const music::Tick outputTick = segment.outputStart + (change.tick - segment.sourceStart);
                PlaybackEvent program;
                program.timestampUs = m_timeline->outputTickToMicroseconds(outputTick);
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
                event.timestampUs = m_timeline->outputTickToMicroseconds(outputTick);
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
                event.timestampUs = m_timeline->outputTickToMicroseconds(outputTick);
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
                event.timestampUs = m_timeline->outputTickToMicroseconds(outputTick);
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
                event.timestampUs = m_timeline->outputTickToMicroseconds(outputTick);
                event.channel = change.channel;
                event.pitch = change.pitch;
                event.value = change.value;
                event.sequence = change.sequence;
                event.kind = PlaybackEventKind::PolyPressure;
                data.events.push_back(event);
            }
        }
        data.events = renderer.render(data);

        // NoteOff 与 NoteOn/控制事件共用时间排序流。后端必须收到明确 NoteOff，
        // 不能通过 UI 定时器释放，否则会引入不可控抖动。
        QVector<PlaybackEvent> noteOffEvents;
        // 在表达和连音渲染之后派生释放事件，确保 NoteOff 采用最终演奏时值。
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
        // 主流和 NoteOff 流分别建立索引；合并流用于快照和全局状态重建。
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

    // 建立全曲唯一事件流用于共享通道状态重建。轨道局部流负责调度，
    // 全局流负责 seek/playback 上下文，避免不同轨道覆盖顺序不稳定。
    // 合并时重新分配序号，使不同逻辑轨道在同时间戳/优先级下也拥有确定的全局顺序。
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

// 返回模型内部的只读轨道；未找到时为 nullptr，指针有效期与模型一致。
const PlaybackData* PlaybackModel::track(const QString& id) const
{
    // 轨道数量通常较少，线性查找可保持构建顺序并避免额外索引存储。
    for (const auto& track : m_tracks) if (track.trackId == id) return &track;
    return nullptr;
}

} // namespace midi_play::playback
