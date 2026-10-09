#pragma once

#include <QString>
#include <QVector>
#include <cstdint>

// 统一音乐文档、事件值对象、tempo map 和重复播放段的领域接口。
// 音乐领域的统一数据模型。
//
// 本文件只描述与输入格式无关的音乐事实：音符、轨道、节拍、调号、
// 小节、歌词和导航标记。MIDI、MusicXML 等基础设施负责把外部格式
// 转换为这些结构；播放和可视化层再以只读方式消费 MusicDocument。
namespace midi_play::music {

// 音乐时间轴上的离散位置，单位为 PPQ tick。
using Tick = std::int64_t;

// 同一时间点的原始 tick 和其对应的绝对微秒位置。
// raw 时间用于保留导入/分析结果，不替代文档的主时间轴。
struct RawTime {
    Tick tick = 0;
    qint64 microseconds = 0;
};

// 音符在小节网格中的定位信息。
struct GridTime {
    Tick tick = 0;
    int measureIndex = -1;
    double beat = 0.0;
};

// 记谱音高及其对应的 MIDI 音高。
// step 使用 0=C ... 6=B，alter 表示升降号数量。
struct WrittenPitch {
    int step = 0; // C=0 ... B=6
    int alter = 0;
    int octave = 4;
    int midiPitch = 60;
};

// 当前文档的调性上下文，用于把 MIDI 音高映射为音级和记谱拼写。
struct KeyContext {
    int fifths = 0;
    QString mode = QStringLiteral("major");

    // 返回主音的 MIDI pitch class，范围为 0..11。
    int tonicPitchClass() const;
    // 返回给定记谱音高最接近的调内音级，范围为 1..7。
    int degreeFor(const WrittenPitch& pitch) const;
};

// 在源 tick 位置发生的速度变化。sequence 用于稳定同 tick 事件的覆盖顺序。
struct TempoChange {
    Tick tick = 0;
    double bpm = 120.0;
    quint64 sequence = 0;
};

// 统一后的音符事件。
//
// duration 表示键释放时刻；sustainEnd 若有效，则表示踏板处理后的可听
// 结束位置。输入文件未提供的派生字段由 MusicAnalyzer 补齐。
struct NoteEvent {
    // start/duration 均为源文档 tick；pitch/velocity/channel 使用 MIDI 语义。
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
    bool marcato = false;
    bool tremolo = false;
    // sequence 保留同 tick 导入事件的稳定顺序，noteId 在文档内标识音符。
    quint64 sequence = 0;
    quint64 noteId = 0;
    RawTime rawStart;
    RawTime rawEnd;
    GridTime gridStart;
    GridTime gridEnd;
    // 以下字段由 MusicAnalyzer 或导入器补充，服务于元数据和可视化。
    WrittenPitch writtenPitch;
    int scaleDegree = 0;
    int accidental = 0;
    int measureIndex = -1;
    double beat = 0.0;
    quint64 chordId = 0;
    quint64 tieGroupId = 0;
    int lane = -1;
    bool hasWrittenPitch = false;
    // Imported performance timing, in source ticks. A negative value means
    // pedal duration must be resolved from score controller events. duration
    // always ends at key release, never at pedal release.
    Tick sustainEnd = -1;
};

// 一个源乐谱小节以及其反复/导航标记。
struct Measure {
    // 小节在源时间轴上的半开区间 [start, start + duration)。
    int number = 0;
    Tick start = 0;
    Tick duration = 0;
    bool repeatStart = false;
    bool repeatEnd = false;
    int repeatCount = 2;
    int endingNumber = 0;
    QString endingType;
    bool segno = false;
    bool coda = false;
    bool daCapo = false;
    bool dalSegno = false;
    bool toCoda = false;
    bool fine = false;
    bool implicit = false; // Incomplete/unnumbered MusicXML measure, including pickups.
};

// 轨道在指定 tick 的乐器、通道或音色库切换。
struct InstrumentChange {
    Tick tick = 0;
    int channel = 0;
    int program = 0;
    QString sourceId;
    int bankMsb = 0;
    int bankLsb = 0;
    quint64 sequence = 0;
};

// MIDI 控制器变化，例如延音踏板、音量和表达控制。
struct ControlChange {
    Tick tick = 0;
    int channel = 0;
    int controller = 0;
    int value = 0;
    quint64 sequence = 0;
};

// MIDI pitch bend 变化，value 使用 MIDI 14 位中心值 8192。
struct PitchBendChange {
    Tick tick = 0;
    int channel = 0;
    int value = 8192;
    quint64 sequence = 0;
};

// 通道压力变化。
struct ChannelPressureChange {
    Tick tick = 0;
    int channel = 0;
    int value = 0;
    quint64 sequence = 0;
};

// 单音符压力变化。
struct PolyPressureChange {
    Tick tick = 0;
    int channel = 0;
    int pitch = 60;
    int value = 0;
    quint64 sequence = 0;
};

// 由谱面动态记号或导入数据产生的力度变化。
struct DynamicChange {
    Tick tick = 0;
    int velocity = 90;
};

// 拍号变化及其 MIDI 节拍元数据和可选加法节拍分组。
struct TimeSignatureChange {
    Tick tick = 0;
    int beats = 4;
    int beatType = 4;
    int metronomeClocks = 0;
    int notated32nds = 8;
    QVector<int> beatGroups; // Optional additive meter, e.g. 3+2/8.
};

// 显式节拍单位变化，quarterNotes 表示一个点击包含多少个四分音符。
struct MetronomeUnitChange {
    Tick tick = 0;
    double quarterNotes = 1.0;
};

// 调号变化，fifths 为五度圈上的升降号数量。
struct KeySignatureChange {
    Tick tick = 0;
    int fifths = 0;
    QString mode = QStringLiteral("major");
};

// 谱表变化，供谱面投影保留原始记谱上下文。
struct ClefChange {
    Tick tick = 0;
    int staff = 1;
    QString sign = QStringLiteral("G");
    int line = 2;
    int octaveChange = 0;
};

// 渐强或渐弱的起止区间。
struct HairpinChange {
    Tick start = 0;
    Tick end = 0;
    bool crescendo = true;
};

// 同一声部、谱表和起始 tick 上的一组和弦音符。
struct ChordGroup {
    quint64 id = 0;
    Tick start = 0;
    Tick duration = 0;
    int voice = 1;
    int staff = 1;
    QVector<quint64> noteIds;
};

// 可视化或播放层使用的持续音符区间。
struct HoldNote {
    quint64 noteId = 0;
    Tick start = 0;
    Tick end = 0;
    int lane = -1;
};

// 通过 tieStart/tieStop 连接的一组音符 ID。
struct TieGroup {
    quint64 id = 0;
    QVector<quint64> noteIds;
};

// 打击乐 MIDI 音高到显示 lane 的映射。
struct DrumMapEntry {
    int pitch = 0;
    QString name;
    int lane = 0;
    int staffLine = 0;
};

// 乐曲中的文本标记。
struct MarkerEvent {
    Tick tick = 0;
    QString text;
    quint64 sequence = 0;
};

// 歌词事件及其主歌/副歌行号。
struct LyricEvent {
    Tick tick = 0;
    QString text;
    int verse = 1;
    quint64 sequence = 0;
};

// 源乐谱区间在反复展开后输出时间线中的映射。
struct PlaybackSegment {
    Tick sourceStart = 0;
    Tick sourceEnd = 0;
    Tick outputStart = 0;
    int repeatPass = 0;
    int sourceMeasureIndex = -1;
};

// RepeatList 构建过程中的跳转状态。
struct RepeatJumpContext {
    int repeatStartIndex = 0;
    int repeatPass = 0;
    bool daCapoTaken = false;
    bool dalSegnoTaken = false;
    bool codaArmed = false;
};

// 按小节导航标记展开源乐谱。
//
// 结果不复制音符，只记录源区间到输出区间的映射，供播放和可视化层
// 在同一套反复语义上生成事件。
class RepeatList final {
public:
    // 根据小节标记构建展开段；duration 用于没有小节信息时的兜底区间。
    static RepeatList build(const QVector<Measure>& measures, Tick duration);

    // 返回按播放顺序排列的源区间映射。
    const QVector<PlaybackSegment>& segments() const { return m_segments; }
    // 返回展开后输出时间线的总 tick 数。
    Tick duration() const { return m_duration; }

private:
    QVector<PlaybackSegment> m_segments;
    Tick m_duration = 0;
};

// 一个可播放轨道及其所有与轨道相关的音乐事件。
struct Track {
    // Track 的 notes 和控制事件均使用源文档 tick，显示/播放顺序由消费者投影。
    QString id;
    QString name;
    int channel = 0;
    int program = 0;
    bool percussion = false;
    QVector<NoteEvent> notes;
    QVector<Measure> measures;
    QVector<InstrumentChange> instrumentChanges;
    QVector<ControlChange> controlChanges;
    QVector<PitchBendChange> pitchBendChanges;
    QVector<ChannelPressureChange> channelPressureChanges;
    QVector<PolyPressureChange> polyPressureChanges;
    QVector<DynamicChange> dynamics;
    QVector<TimeSignatureChange> timeSignatures;
    QVector<KeySignatureChange> keySignatures;
    QVector<ClefChange> clefs;
    QVector<HairpinChange> hairpins;
    QVector<ChordGroup> chords;
    QVector<HoldNote> holds;
    QVector<TieGroup> ties;
    QVector<DrumMapEntry> drumMap;
    QVector<MarkerEvent> markers;
    QVector<LyricEvent> lyrics;
};

// 输入格式归一化后的完整音乐文档。
//
// 导入阶段文档可变；交给 PlaybackModel、VisualChart 或 PlaybackTimeline
// 后应通过 shared_ptr<const MusicDocument> 只读共享。主时间基准固定为
// kPpq tick，绝对播放时间统一由 tempo map 换算为微秒。
class MusicDocument {
public:
    static constexpr int kPpq = 480;

    // 有轨道且持续时间为正才视为可播放文档。
    bool isValid() const { return !m_tracks.isEmpty() && m_duration > 0; }
    // 访问轨道集合；非 const 版本只应在导入/分析阶段使用。
    const QVector<Track>& tracks() const { return m_tracks; }
    QVector<Track>& tracks() { return m_tracks; }
    // 访问文档级小节网格。
    const QVector<Measure>& measures() const { return m_measures; }
    QVector<Measure>& measures() { return m_measures; }
    // 用各轨道中最完整的小节表重建文档级小节网格。
    void rebuildMeasureGrid();
    const QVector<KeySignatureChange>& keySignatures() const { return m_keySignatures; }
    QVector<KeySignatureChange>& keySignatures() { return m_keySignatures; }
    // 访问文档级标记和歌词；元素按导入顺序保存，消费者可再按 tick 排序。
    const QVector<MarkerEvent>& markers() const { return m_markers; }
    QVector<MarkerEvent>& markers() { return m_markers; }
    const QVector<LyricEvent>& lyrics() const { return m_lyrics; }
    QVector<LyricEvent>& lyrics() { return m_lyrics; }
    const KeyContext& keyContext() const { return m_keyContext; }
    // 设置分析后的调性上下文。
    void setKeyContext(const KeyContext& value) { m_keyContext = value; }
    // 访问 tempo 变化；非 const 修改会使内部 tempo map 失效。
    const QVector<TempoChange>& tempos() const { return m_tempos; }
    // 修改 tempo 后清除惰性缓存，下一次换算时重新构建 tempo map。
    QVector<TempoChange>& tempos() { m_tempoMap.clear(); return m_tempos; }
    // 在只读消费者启动前显式构建 tempo map。
    void rebuildTempoMap() const;
    // SMPTE ticks encode absolute time; the synthetic tempo used for their
    // conversion must never be interpreted as a musical beat grid.
    bool hasMusicalTimebase() const { return m_hasMusicalTimebase; }
    void setMusicalTimebase(bool musical) { m_hasMusicalTimebase = musical; }
    // MIDI format 2 各独立序列在统一时间轴上的起点。
    QVector<Tick>& sequenceStarts() { return m_sequenceStarts; }
    const QVector<Tick>& sequenceStarts() const { return m_sequenceStarts; }
    QVector<MetronomeUnitChange>& metronomeUnits() { return m_metronomeUnits; }
    const QVector<MetronomeUnitChange>& metronomeUnits() const { return m_metronomeUnits; }
    // 文档源时长，单位为 tick。
    Tick duration() const { return m_duration; }
    void setDuration(Tick value) { m_duration = value; }
    // 输入文件提供的标题。
    QString title() const { return m_title; }
    void setTitle(const QString& value) { m_title = value; }

    // 按源时间轴的 tempo map 将 tick 转为绝对微秒，输入会被限制在文档范围。
    qint64 tickToMicroseconds(Tick tick) const;
    // 将源时间轴的微秒位置反向映射为 tick。
    Tick microsecondsToTick(qint64 microseconds) const;
    // 将反复展开后的输出 tick 转为播放经过的微秒。
    qint64 playbackTickToMicroseconds(Tick outputTick) const;
    // 返回按播放顺序展开的源区间。
    QVector<PlaybackSegment> playbackSegments() const;
    // 返回反复展开后的输出时长。
    Tick playbackDuration() const;

private:
    QVector<Track> m_tracks;
    QVector<TempoChange> m_tempos;
    QVector<MetronomeUnitChange> m_metronomeUnits;
    QVector<Tick> m_sequenceStarts;
    bool m_hasMusicalTimebase = true;
    Tick m_duration = 0;
    QString m_title;
    QVector<Measure> m_measures;
    QVector<KeySignatureChange> m_keySignatures;
    QVector<MarkerEvent> m_markers;
    QVector<LyricEvent> m_lyrics;
    KeyContext m_keyContext;
    // 按源 tick 分段的惰性 tempo 累计缓存；只读换算会填充它。
    struct TempoSegment {
        Tick start = 0;
        Tick end = 0;
        qint64 startUs = 0;
        double bpm = 120.0;
    };
    mutable QVector<TempoSegment> m_tempoMap;
};

} // namespace midi_play::music
