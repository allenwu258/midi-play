#include "mididocumentbuilder.h"
// 将归一化 MIDI 数据映射到领域轨道，并构建可共享的音乐时间索引。
#include "domain/music/musicanalysis.h"

#include "midinormalizer.h"

#include <algorithm>

namespace midi_play::midi {
namespace {

// 提取全局拍号、调号、标记和歌词；谱表级拍号/调号写入首个保留轨道。
void appendMeta(const MidiNormalizedFile& source, music::MusicDocument& document)
{
    for (const auto& event : source.globalEvents) {
        if (event.kind != MidiMessageKind::Meta) continue;
        if (event.metaType == 0x58 && event.payload.size() >= 2) {
            const int beats = static_cast<unsigned char>(event.payload.at(0));
            // MIDI 拍号分母以 2 的指数编码；过大指数用零标记为非法。
            const int denominatorPower = static_cast<unsigned char>(event.payload.at(1));
            const music::TimeSignatureChange change {event.tick, beats,
                                                      denominatorPower <= 10 ? 1 << denominatorPower : 0,
                                                      event.payload.size() >= 3
                                                          ? static_cast<unsigned char>(event.payload.at(2)) : 0,
                                                      event.payload.size() >= 4
                                                          ? static_cast<unsigned char>(event.payload.at(3)) : 8};
            if (!document.tracks().isEmpty()) document.tracks().front().timeSignatures.push_back(change);
        } else if (event.metaType == 0x59 && event.payload.size() >= 2) {
            const int fifths = static_cast<qint8>(event.payload.at(0));
            const int mode = static_cast<unsigned char>(event.payload.at(1));
            const music::KeySignatureChange change {event.tick, fifths,
                mode == 0 ? QStringLiteral("major") : QStringLiteral("minor")};
            document.keySignatures().push_back(change);
            if (!document.tracks().isEmpty()) document.tracks().front().keySignatures.push_back(change);
        } else if ((event.metaType == 0x06 || event.metaType == 0x05) && !event.payload.isEmpty()) {
            const QString text = QString::fromUtf8(event.payload.constData(), event.payload.size());
            if (event.metaType == 0x06) document.markers().push_back({event.tick, text, event.sequence});
            else document.lyrics().push_back({event.tick, text, 1, event.sequence});
        }
    }
}

} // namespace

// 构建轨道与控制事件，再补齐文档元数据和分析结果。
music::ReadResult MidiDocumentBuilder::build(const MidiNormalizedFile& source) const
{
    auto document = std::make_shared<music::MusicDocument>();
    document->setTitle(source.title);
    document->tempos() = source.tempos;
    // SMPTE 文档仍可播放，但没有可信的音乐拍点，不能据此启用节拍器网格。
    document->setMusicalTimebase(!source.header.smpte);
    document->sequenceStarts() = source.sequenceStarts;
    quint64 nextNoteId = 1;

    for (const auto& normalizedTrack : source.tracks) {
        music::Track track;
        track.id = normalizedTrack.id;
        track.name = normalizedTrack.name;
        track.channel = normalizedTrack.channel;
        track.program = normalizedTrack.program;
        track.percussion = normalizedTrack.percussion;

        int bankMsb = 0;
        int bankLsb = 0;
        // Bank Select 是通道状态；每次 Program Change 都携带当时生效的两个 bank 字节。
        for (const auto& event : normalizedTrack.events) {
            switch (event.kind) {
            case MidiMessageKind::ControlChange:
                track.controlChanges.push_back({event.tick, event.channel, event.data1, event.data2, event.sequence});
                if (event.data1 == 0) bankMsb = std::clamp(event.data2, 0, 127);
                if (event.data1 == 32) bankLsb = std::clamp(event.data2, 0, 127);
                break;
            case MidiMessageKind::ProgramChange:
                track.instrumentChanges.push_back({event.tick, event.channel,
                                                    std::clamp(event.data1, 0, 127),
                                                    normalizedTrack.id, bankMsb, bankLsb, event.sequence});
                break;
            case MidiMessageKind::PitchBend:
                track.pitchBendChanges.push_back({event.tick, event.channel,
                                                  std::clamp(event.data2, 0, 16383), event.sequence});
                break;
            case MidiMessageKind::ChannelPressure:
                track.channelPressureChanges.push_back({event.tick, event.channel,
                                                         std::clamp(event.data1, 0, 127), event.sequence});
                break;
            case MidiMessageKind::PolyPressure:
                track.polyPressureChanges.push_back({event.tick, event.channel,
                                                      std::clamp(event.data1, 0, 127),
                                                      std::clamp(event.data2, 0, 127), event.sequence});
                break;
            default:
                break;
            }
        }

        // duration 只表示按键时值，sustainEnd 保留踏板导致的可听结束时间。
        for (const auto& note : normalizedTrack.notes) {
            const music::Tick end = std::max(note.start + 1, note.keyRelease);
            music::NoteEvent event;
            event.start = note.start;
            event.duration = std::max<music::Tick>(1, end - note.start);
            event.sustainEnd = std::max(end, note.audibleEnd);
            event.pitch = note.pitch;
            event.velocity = note.velocity;
            event.channel = note.channel;
            event.program = note.program;
            event.sequence = note.sequence;
            event.noteId = nextNoteId++;
            track.notes.push_back(event);
        }
        std::sort(track.notes.begin(), track.notes.end(), [](const auto& left, const auto& right) {
            if (left.start != right.start) return left.start < right.start;
            if (left.pitch != right.pitch) return left.pitch < right.pitch;
            if (left.velocity != right.velocity) return left.velocity < right.velocity;
            return left.sequence < right.sequence;
        });
        // 不保留纯控制轨道；全局元数据仍由 globalEvents 单独提取。
        if (!track.notes.isEmpty()) document->tracks().push_back(std::move(track));
    }

    appendMeta(source, *document);
    document->setDuration(std::max<music::Tick>(1, source.duration));
    // 完成可供播放与可视化共享的网格、速度积分和派生音乐语义。
    document->rebuildMeasureGrid();
    document->rebuildTempoMap();
    music::MusicAnalyzer().analyze(*document);
    if (!document->isValid()) return {nullptr, QStringLiteral("MIDI 未包含可播放音符")};
    return {std::move(document), {}};
}

} // namespace midi_play::midi
