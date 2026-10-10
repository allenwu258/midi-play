#pragma once

// MIDI 导入的中间数据：原始消息、配对音符和归一化轨道，供解析流水线逐阶段使用。
#include "domain/music/musicdocument.h"

#include <QByteArray>
#include <QVector>
#include <QString>
#include <QHash>

#include <cstdint>
#include <memory>

namespace midi_play::midi {

// SMF 文件头；division 是源时间分辨率，SMPTE 文件另保存帧率和每帧 tick 数。
struct MidiFileHeader {
    int format = 1; // 0 为单轨、1 为同步多轨、2 为独立序列。
    int trackCount = 0; // 文件头声明的 MTrk 数量。
    int division = 480; // PPQ 或解析器推导的 SMPTE 每秒 tick 数，必须为正。
    bool smpte = false;
    int smpteFramesPerSecond = 0; // 当前解析器采用整数帧率，将源 29 码按 30 fps 处理。
    int ticksPerFrame = 0;
};

// 解析器识别的消息类别；力度为零的 NoteOn 在解析阶段归为 NoteOff。
enum class MidiMessageKind {
    NoteOn,
    NoteOff,
    PolyPressure,
    ControlChange,
    ProgramChange,
    ChannelPressure,
    PitchBend,
    Meta,
    SysEx
};

// 单条消息的来源、时间和负载。解析阶段使用源 tick，归一化阶段换为文档 tick。
struct MidiRawEvent {
    int sourceTrack = 0; // 物理轨道的零基下标。
    int port = 0; // MIDI Port 元事件选中的端口，不与 channel 合并。
    int channel = 0; // 通道消息的零基编号；Meta/SysEx 不以此字段路由。
    midi_play::music::Tick tick = 0;
    quint64 sequence = 0; // 同时刻消息的稳定排序依据。
    MidiMessageKind kind = MidiMessageKind::Meta;
    int data1 = 0;
    int data2 = 0; // PitchBend 在此存放合并后的 14 位值，其他消息保留第二数据字节。
    int metaType = -1; // 仅 Meta 消息有效，-1 表示非元事件。
    QByteArray payload; // Meta/SysEx 的原始负载，供文本及专用元事件解码使用。
};

// 一个 MTrk 块，保留物理轨道顺序和未经音符配对的消息。
struct MidiRawTrack {
    int index = 0;
    QString name;
    QVector<MidiRawEvent> events;
};

// 字节解析结果，尚未拆分逻辑通道或处理踏板延音。
struct MidiParsedFile {
    MidiFileHeader header;
    QVector<MidiRawTrack> tracks;
    QString title;
};

// NoteOn/NoteOff 配对结果：区分按键释放时刻和踏板保持后的可听结束时刻。
struct MidiNoteSpan {
    int sourceTrack = 0;
    int port = 0;
    int channel = 0;
    int pitch = 60;
    int velocity = 90;
    int program = 0; // 记录 NoteOn 时生效的音色，避免后续音色变化覆盖历史音符。
    music::Tick start = 0; // 以下时间均采用文档 tick，结束边界由按键配对及踏板状态机结算。
    music::Tick keyRelease = 0; // 按键释放边界，用于播放 NoteOff。
    music::Tick audibleEnd = 0; // 踏板保持后的可听边界，用于时长汇总和可视化。
    quint64 sequence = 0;
    bool implicitOff = false; // 未找到 NoteOff 时由归一化器在通道尾部补齐。
};

// 按物理轨道、端口和通道分组的逻辑轨道，时间已统一到 MusicDocument 的 tick 基准。
struct MidiLogicalTrack {
    QString id;
    QString name;
    int sourceTrack = 0;
    int port = 0;
    int channel = 0;
    int program = 0;
    int bankMsb = 0;
    int bankLsb = 0;
    bool percussion = false;
    QVector<MidiNoteSpan> notes;
    QVector<MidiRawEvent> events;
};

// 导入时可容忍异常的计数，不直接决定文档是否可播放。
struct MidiReadDiagnostics {
    int orphanNoteOffs = 0;
    int implicitNoteOffs = 0;
    int unknownMessages = 0;
};

// 文档构建器的输入；全局消息用于元数据，轨道消息和音符用于演奏内容。
struct MidiNormalizedFile {
    MidiFileHeader header;
    QVector<MidiLogicalTrack> tracks;
    QVector<MidiRawEvent> globalEvents;
    QVector<music::TempoChange> tempos;
    QVector<music::Tick> sequenceStarts; // format 2 各独立序列拼接后的起点。
    QString title;
    music::Tick duration = 0;
    MidiReadDiagnostics diagnostics;
};

} // namespace midi_play::midi
