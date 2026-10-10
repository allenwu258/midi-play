#pragma once

// 播放域共享值对象、事件类型、排序规则和状态索引定义。
#include "domain/music/musicdocument.h"

#include <QString>
#include <QHash>
#include <QMetaType>
#include <QVector>
#include <memory>
#include <algorithm>
#include <utility>

namespace midi_play::playback {

// 播放会话的生命周期状态。Empty 表示尚未绑定文档，Ready 表示已创建会话，
// Playing/Paused/Stopped 对应传输状态，Error 表示音频后端或恢复流程失败。
enum class State { Empty, Ready, Playing, Paused, Stopped, Error };

// 可被音频后端加载的音源资源；soundFontId 由具体后端在加载后填充。
struct AudioResource {
    // 外部音源文件路径；资源加载和可读性检查由基础设施层负责。
    QString path;
    // FluidSynth 等后端分配的句柄，-1 表示尚未获得有效资源编号。
    int soundFontId = -1;
};

// 音源资源的展示元数据和扩展属性。
struct AudioResourceMeta {
    QString id;
    QString vendor;
    QString name;
    QString type = QStringLiteral("soundfont");
    QString path;
    QHash<QString, QString> attributes;
};

// 一个可选的音色预置，使用 MIDI bank/program 标识。
struct SoundPreset {
    QString id;
    QString name;
    int bank = 0; // Bank Select 合成的音色库编号。
    int program = 0; // 从零开始的 MIDI program，通常为 0..127。
};

// 文档或轨道选择音源时使用的稳定配置值。
struct PlaybackSetupData {
    QString soundId = QStringLiteral("piano");
    QString musicXmlSoundId;
    bool supportsSingleNoteDynamics = false;
};

// 播放事件的协议类型。事件按微秒时间戳进入调度器，具体编码由音频后端完成。
enum class PlaybackEventKind {
    NoteOn, // 按指定力度触发音符，并携带用于派生 NoteOff 的时值。
    NoteOff, // 释放按键；踏板是否继续保持声音由当前通道状态决定。
    ProgramChange, // 在指定 bank 中选择 program。
    ControlChange, // 设置编号为 controller 的 MIDI 控制器。
    PitchBend, // 通道弯音，value 为 14 位无符号值，8192 为中心。
    ChannelPressure, // 通道触后，value 为 7 位压力值。
    PolyPressure, // 指定 pitch 的按键触后。
    NoteExpression, // 单音符表达，由 expressionType 确定 value 的含义。
    AllNotesOff // 请求后端清理发声状态，具体复位范围由后端契约确定。
};

// 单音符表达控制的语义类型。
enum class NoteExpressionType {
    Pressure,
    Timbre,
    Pitch,
    Volume
};

// 由记谱或导入信息应用到音符上的演奏法。
enum class ArticulationType {
    Staccato,
    Accent,
    Tenuto,
    Ghost,
    Marcato,
    Tremolo
};

// 一个演奏法对时值、力度的修正结果。
struct ArticulationAppliedData {
    ArticulationType type = ArticulationType::Tenuto;
    double durationFactor = 1.0; // 相对于原音符时值的乘数。
    double velocityFactor = 1.0; // 原 MIDI velocity 的乘数，偏移在乘法后应用。
    int velocityOffset = 0; // 力度整数偏移，最终事件由渲染器夹取到有效范围。
};

// 表达曲线中的相对时间点；offsetUs 相对于所属音符起始时间。
struct ExpressionCurvePoint {
    qint64 offsetUs = 0;
    // 数值随曲线解释：pitchCurve 为 [-1, 1] 归一化弯音，expressionCurve 为归一化力度。
    float value = 0.0F;
};

// 音符的表达上下文，既可被支持 per-note expression 的后端消费，
// 也可由渲染器转换为传统 MIDI 控制事件。
struct NoteExpressionContext {
    QVector<ArticulationAppliedData> articulations;
    QVector<ExpressionCurvePoint> pitchCurve;
    QVector<ExpressionCurvePoint> expressionCurve;
};

// 调度器向音频后端提交的统一事件对象。
// NoteOn 的 durationUs 用于生成明确的 NoteOff；其他事件通常为瞬时事件。
struct PlaybackEvent {
    // 反复展开后的音乐时间，单位为微秒；播放倍率改变墙钟映射，不改写此时间戳。
    qint64 timestampUs = 0;
    // 按键时值，单位为微秒；不能直接作为踏板延音后的可听时长。
    qint64 durationUs = 0;
    // 从零开始的 MIDI 通道，通常为 0..15；本结构不自行做范围校验。
    int channel = 0;
    // NoteOn/NoteOff/PolyPressure 的目标音高，通常为 0..127。
    int pitch = 60;
    // NoteOn 的 7 位力度；其他事件不消费此字段。
    int velocity = 90;
    // ProgramChange 的零基音色编号；音符事件也可保存来源音色。
    int program = 0;
    // 以下标志保存记谱演奏语义，供表达渲染器调整事件，而非直接编码为 MIDI 字节。
    bool tieStart = false;
    bool tieStop = false;
    bool staccato = false;
    bool accent = false;
    bool tenuto = false;
    bool ghost = false;
    PlaybackEventKind kind = PlaybackEventKind::NoteOn;
    // 同时刻、同优先级事件的排序依据；反复实例可沿用同一源序号。
    quint64 sequence = 0;
    // 仅 ControlChange 使用的 7 位控制器编号。
    int controller = 0;
    // 控制值随 kind/expressionType 解释，弯音范围与压力/控制器范围不同。
    int value = 0;
    // 导入后的记谱声部和谱表编号，用于分组及连音匹配，不等于 MIDI channel。
    int voice = 1;
    int staff = 1;
    bool marcato = false;
    bool tremolo = false;
    NoteExpressionContext expression;
    // Bank Select 的两个控制器与 7 位 program 分开保存，使跳转后的状态重放无损。
    int bankMsb = 0;
    int bankLsb = 0;
    // seek 恢复音符时区分仍按键与已松键但受踏板保持的声音。
    bool keyReleased = false;
    // 文档音符身份；-1 时使用通道/音高回退匹配，反复播放不保证此 ID 每次唯一。
    qint64 noteId = -1;
    NoteExpressionType expressionType = NoteExpressionType::Volume;

    bool isNoteOn() const { return kind == PlaybackEventKind::NoteOn; }
    bool isNoteOff() const { return kind == PlaybackEventKind::NoteOff; }
};

// 判断同一时间戳事件的 MIDI 协议优先级。
// NoteOff 先于音符重触发，Bank Select 先于 Program Change，保证合成器观察到
// 正确的通道状态顺序。
inline int playbackEventPriority(const PlaybackEvent& event)
{
    if (event.kind == PlaybackEventKind::NoteOff || event.kind == PlaybackEventKind::AllNotesOff) return 0;
    if (event.kind == PlaybackEventKind::ControlChange
        && (event.controller == 0 || event.controller == 32)) return 1;
    if (event.kind == PlaybackEventKind::ProgramChange) return 2;
    if (event.kind == PlaybackEventKind::ControlChange
        || event.kind == PlaybackEventKind::PitchBend
        || event.kind == PlaybackEventKind::ChannelPressure
        || event.kind == PlaybackEventKind::PolyPressure
        || event.kind == PlaybackEventKind::NoteExpression) return 3;
    return 4;
}

// 构建期可变的事件流与平行时间戳索引；finalize 后才满足二分查询的排序前提。
class PlaybackEventMap final {
public:
    // 清除事件和用于二分查找的时间戳索引。
    void clear()
    {
        m_events.clear();
        m_timestamps.clear();
    }

    // 用已有事件替换容器；调用方应在 finalize() 后使用 lowerBound()。
    void assign(const QVector<PlaybackEvent>& events)
    {
        m_events = events;
        m_timestamps.clear();
        m_timestamps.reserve(m_events.size());
        for (const auto& event : m_events) {
            m_timestamps.push_back(event.timestampUs);
        }
    }

    // 追加一个尚未排序的事件，适用于模型构建阶段。
    void insert(PlaybackEvent event)
    {
        m_events.push_back(std::move(event));
    }

    // 按时间戳、协议优先级和序号排序，并重建时间戳索引。
    void finalize()
    {
        std::sort(m_events.begin(), m_events.end(), [](const auto& left, const auto& right) {
            if (left.timestampUs != right.timestampUs) return left.timestampUs < right.timestampUs;
            const int leftPriority = playbackEventPriority(left);
            const int rightPriority = playbackEventPriority(right);
            if (leftPriority != rightPriority) return leftPriority < rightPriority;
            return left.sequence < right.sequence;
        });
        m_timestamps.clear();
        m_timestamps.reserve(m_events.size());
        for (const auto& event : m_events) m_timestamps.push_back(event.timestampUs);
    }

    // 返回已完成排序的事件序列。
    const QVector<PlaybackEvent>& events() const { return m_events; }
    // 判断事件流是否为空。
    bool isEmpty() const { return m_events.isEmpty(); }
    // 返回事件数量。
    int size() const { return m_events.size(); }
    // 以边界检查方式访问事件。
    const PlaybackEvent& at(int index) const { return m_events.at(index); }

    // 返回第一个时间戳不小于目标值的事件下标。
    int lowerBound(qint64 timestampUs) const
    {
        return static_cast<int>(std::lower_bound(m_timestamps.cbegin(), m_timestamps.cend(), timestampUs)
                                - m_timestamps.cbegin());
    }

private:
    QVector<PlaybackEvent> m_events;
    // finalize/assign 按事件顺序生成；insert 不更新该索引。
    QVector<qint64> m_timestamps;
};

// 跳转或恢复播放时需要重建的单个 MIDI 通道状态。
struct ChannelState {
    // 是否曾收到通道状态事件；恢复时只重放已初始化通道。
    bool initialized = false;
    // 当前音色选择与通道控制值沿用 MIDI 编码；pitchBend 以 8192 为中心。
    int program = 0;
    int bankMsb = 0;
    int bankLsb = 0;
    int pitchBend = 8192;
    int channelPressure = 0;
    QHash<int, int> polyPressure;
    // RPN/NRPN 选择器分别保存；127/127 是 RPN 空选择约定。
    int rpnMsb = 127;
    int rpnLsb = 127;
    int nrpnMsb = 127;
    int nrpnLsb = 127;
    // CC6/CC38 的粗调/细调值；RPN 0/0 时派生为弯音范围。
    int dataEntryMsb = 0;
    int dataEntryLsb = 0;
    // 弯音灵敏度的半音数和音分数，独立于当前 pitchBend 值。
    int pitchBendRangeSemitones = 2;
    int pitchBendRangeCents = 0;
    // 已观察到的控制器原值；恢复时仅重放已记录项。
    QHash<int, int> controllers;
};

// 快照时刻仍处于发声状态的音符及其踏板锁存信息。
struct ActiveNoteState {
    qint64 noteId = -1;
    int channel = 0;
    int pitch = 60;
    int velocity = 90;
    qint64 endTimestampUs = 0; // 输出时间线上的预计结束位置，单位为微秒。
    bool keyReleased = false; // 已收到 NoteOff，但声音可能仍由踏板保持。
    bool sostenutoCaptured = false; // CC66 捕获的按键身份，不等同于已进入延音阶段。
    bool sustainLatched = false; // 松键时被 CC64 持续踏板保持。
    bool sostenutoLatched = false; // 松键时被 CC66 选择性延音踏板保持。
};

// 在某一时间点保存的通道状态、活动音符和全局事件下标。
// PlaybackSession 使用它减少 seek 时需要重放的事件数量。
struct PlaybackStateSnapshot {
    qint64 timestampUs = 0;
    // 尚未应用的首个事件下标；待重放范围包含检查点同时间戳的事件。
    int eventIndex = 0;
    QHash<int, ChannelState> channels;
    QVector<ActiveNoteState> activeNotes;
};

// 仅保存事件时间戳的只读索引，用于快速定位 seek 起点。
class PlaybackEventIndex final {
public:
    // 从已经按时间排序的事件构建索引。
    explicit PlaybackEventIndex(const QVector<PlaybackEvent>& events)
    {
        m_timestamps.reserve(events.size());
        for (const auto& event : events) m_timestamps.push_back(event.timestampUs);
    }

    // 返回第一个不早于目标时间的事件下标。
    int lowerBound(qint64 timestampUs) const
    {
        return static_cast<int>(std::lower_bound(m_timestamps.cbegin(), m_timestamps.cend(), timestampUs) - m_timestamps.cbegin());
    }

private:
    QVector<qint64> m_timestamps;
};

// 一个逻辑播放轨道（通常是 staff/voice 分组）及其主事件流、NoteOff 流和快照。
struct PlaybackData {
    // 由源轨道、谱表和声部组成的逻辑轨道标识。
    QString trackId;
    PlaybackSetupData setupData;
    QVector<PlaybackEvent> events;
    QVector<PlaybackEvent> offEvents; // 从已渲染 NoteOn 派生的释放流。
    PlaybackEventMap mainStream;
    PlaybackEventMap offStream;
    QVector<PlaybackStateSnapshot> snapshots; // 此逻辑轨道的恢复检查点。
    std::shared_ptr<const PlaybackEventIndex> index;
    std::shared_ptr<const PlaybackEventIndex> offIndex;
};

} // namespace midi_play::playback

Q_DECLARE_METATYPE(midi_play::playback::State)
