#pragma once

// 后端无关的只读视觉图表：展开后的音符、元数据、网格和时间索引。
#include <QMetaType>
#include <QString>
#include <QVector>

#include <cstdint>
#include <memory>

namespace midi_play::visualization {

// 视觉时间统一为展开播放时间线上的微秒。
using VisualTime = qint64;

// 不依赖 Qt 绘制对象的 8 位 RGBA 颜色。
struct ColorRgba {
    quint8 red = 255;
    quint8 green = 255;
    quint8 blue = 255;
    quint8 alpha = 255;
};

// 可组合的演奏/显示标记，保留音符语义供不同后端选择外观。
enum NoteVisualFlag : quint32 {
    NoNoteVisualFlags = 0,
    GraceNote = 1U << 0,
    GhostNote = 1U << 1,
    StaccatoNote = 1U << 2,
    AccentNote = 1U << 3,
    TenutoNote = 1U << 4,
    MarcatoNote = 1U << 5,
    TremoloNote = 1U << 6,
    TiedNote = 1U << 7,
    PercussionNote = 1U << 8
};

// 一次播放出现的视觉音符；同一源音符在反复段中可生成多个实例。
struct VisualNote {
    quint64 instanceId = 0; // 图表内稳定实例 ID，区别于源文档音符 ID。
    quint64 sourceNoteId = 0;
    quint64 chordInstanceId = 0;
    quint64 sourceChordId = 0;
    quint64 tieGroupId = 0;
    int trackIndex = -1;
    int pitch = 60;
    int velocity = 90;
    int channel = 0;
    int staff = 1;
    int voice = 1;
    int sourceMeasureIndex = -1;
    int repeatPass = 0;
    int scaleDegree = 0;
    int accidental = 0;
    int octaveOffset = 0;
    int drumLane = -1;
    int coincidentIndex = 0; // 同音高按键重叠时分配的横向子道，整段保持不变。
    int coincidentCount = 1;
    VisualTime startUs = 0;
    VisualTime keyEndUs = 0; // 按键结束；audibleEndUs 还包含踏板保持部分。
    VisualTime audibleEndUs = 0;
    quint32 flags = NoNoteVisualFlags;
    QString simplifiedLabel;

    // 判断是否使用鼓组道而非钢琴布局。
    bool isPercussion() const { return (flags & PercussionNote) != 0; }
    // 判断是否采用 ghost 弱化外观。
    bool isGhost() const { return (flags & GhostNote) != 0; }
};

// 视觉轨道元数据及指向图表音符数组的下标。
struct VisualTrack {
    QString id;
    QString name;
    ColorRgba color;
    bool percussion = false;
    int channel = 0;
    int program = 0;
    QVector<int> noteIndices;
};

// 展开时间线的节拍/小节线，标签仍引用源小节编号。
struct VisualGridLine {
    VisualTime timeUs = 0;
    int measureNumber = 0;
    int beatIndex = 0;
    bool measureStart = false;
    QString measureLabel;
};

// 覆盖层标记的语义类别。
enum class MarkerKind {
    Text,
    Navigation,
    Tempo,
    KeySignature,
    TimeSignature
};

// 指定播放时刻出现的文字或导航标记。
struct VisualMarker {
    quint64 instanceId = 0;
    VisualTime timeUs = 0;
    QString text;
    MarkerKind kind = MarkerKind::Text;
};

// 展开后的歌词实例与段号。
struct VisualLyric {
    quint64 instanceId = 0;
    VisualTime timeUs = 0;
    QString text;
    int verse = 1;
};

// 从 timeUs 生效的速度，单位为四分音符/分钟。
struct VisualTempoPoint {
    VisualTime timeUs = 0;
    double bpm = 120.0;
};

// 从 timeUs 生效的调号及大小调模式。
struct VisualKeyPoint {
    VisualTime timeUs = 0;
    int fifths = 0;
    QString mode = QStringLiteral("major");
};

// 从 timeUs 生效的拍号，供覆盖层展示。
struct VisualTimeSignaturePoint {
    VisualTime timeUs = 0;
    int beats = 4;
    int beatType = 4;
};

// 将鼓音高映射为连续显示道并保存名称。
struct VisualDrumLane {
    int lane = -1;
    int pitch = 0;
    QString name;
};

// 含保护半音与最小跨度的音高闭区间。
struct PitchRange {
    int minimum = 48;
    int maximum = 71;
};

class PlaybackVisualizationProjector;

// 投影器定稿后只读共享，Qt、Vulkan 与导出使用同一份视觉事实。
class VisualChart final {
public:
    // 返回文档标题或投影时指定的回退标题。
    const QString& title() const { return m_title; }
    // 返回反复展开后的播放时长。
    VisualTime durationUs() const { return m_durationUs; }
    // 返回轨道展示信息。
    const QVector<VisualTrack>& tracks() const { return m_tracks; }
    // 音符按开始时间排序，索引与绘制缓存依赖此顺序。
    const QVector<VisualNote>& notes() const { return m_notes; }
    // 返回按可听结束时间排序的音符下标，不改变主数组。
    const QVector<int>& noteIndicesByEnd() const { return m_noteIndicesByEnd; }
    // 返回投影后的节拍网格。
    const QVector<VisualGridLine>& gridLines() const { return m_gridLines; }
    // 返回有序标记实例。
    const QVector<VisualMarker>& markers() const { return m_markers; }
    // 返回有序歌词实例。
    const QVector<VisualLyric>& lyrics() const { return m_lyrics; }
    // 返回速度变化时间线。
    const QVector<VisualTempoPoint>& tempos() const { return m_tempos; }
    // 返回调号变化时间线。
    const QVector<VisualKeyPoint>& keys() const { return m_keys; }
    // 返回拍号变化时间线。
    const QVector<VisualTimeSignaturePoint>& timeSignatures() const { return m_timeSignatures; }
    // 返回稳定鼓组道映射。
    const QVector<VisualDrumLane>& drumLanes() const { return m_drumLanes; }
    // 返回用于键盘呈现的音域。
    PitchRange pitchRange() const { return m_pitchRange; }
    // 返回加载代次，供消费者识别已替换图表。
    quint64 generation() const { return m_generation; }

    // 查询当前速度，缺失时使用 120 BPM。
    double tempoAt(VisualTime timeUs) const;
    // 查询当前调号，缺失时使用 C 大调。
    VisualKeyPoint keyAt(VisualTime timeUs) const;
    // 查询当前拍号，缺失时使用 4/4。
    VisualTimeSignaturePoint timeSignatureAt(VisualTime timeUs) const;
    // 返回最近已到达标记的文本，首标记之前返回空串。
    QString markerAt(VisualTime timeUs) const;
    // 返回最近已到达歌词的文本，首歌词之前返回空串。
    QString lyricAt(VisualTime timeUs) const;

private:
    friend class PlaybackVisualizationProjector;

    QString m_title;
    VisualTime m_durationUs = 0;
    QVector<VisualTrack> m_tracks;
    QVector<VisualNote> m_notes;
    QVector<int> m_noteIndicesByEnd;
    QVector<VisualGridLine> m_gridLines;
    QVector<VisualMarker> m_markers;
    QVector<VisualLyric> m_lyrics;
    QVector<VisualTempoPoint> m_tempos;
    QVector<VisualKeyPoint> m_keys;
    QVector<VisualTimeSignaturePoint> m_timeSignatures;
    QVector<VisualDrumLane> m_drumLanes;
    PitchRange m_pitchRange;
    quint64 m_generation = 0;
};

// 只读共享所有权，跨线程消费者不能修改投影内容。
using VisualChartPtr = std::shared_ptr<const VisualChart>;

} // namespace midi_play::visualization

Q_DECLARE_METATYPE(midi_play::visualization::VisualChartPtr)
