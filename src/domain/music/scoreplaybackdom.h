#pragma once

#include "musicdocument.h"

#include <QVector>
#include <memory>

// 面向播放和未来谱面渲染的轻量级层级 DOM 接口。
namespace midi_play::music {

// 播放域谱面 DOM 中的一个音符。
struct ScoreDomNote {
    int id = -1;
    NoteEvent value;
};

// 同一 tick、声部下的和弦节点。
struct ScoreDomChord {
    int id = -1;
    Tick tick = 0;
    Tick duration = 0;
    int voice = 1;
    QVector<int> noteIds;
};

// 谱面中的休止节点。
struct ScoreDomRest {
    int id = -1;
    Tick tick = 0;
    Tick duration = 0;
    int voice = 1;
};

// 小节内同一 tick 的和弦/休止集合。
struct ScoreDomSegment {
    Tick tick = 0;
    QVector<int> chordIds;
    QVector<int> restIds;
};

// 一个谱面小节及其时间片。
struct ScoreDomMeasure {
    Measure value;
    QVector<ScoreDomSegment> segments;
};

// 一个谱表及其小节序列。
struct ScoreDomStaff {
    int number = 1;
    QVector<ScoreDomMeasure> measures;
};

// 一个乐谱 part；通常对应输入文档中的一个 Track。
struct ScoreDomPart {
    QString id;
    QString name;
    QVector<ScoreDomStaff> staves;
};

// 渐强、渐弱等跨音符或跨小节的谱面连接对象。
struct ScoreDomSpanner {
    QString type;
    Tick start = 0;
    Tick end = 0;
    int staff = 1;
};

// 面向播放的不可变谱面层级投影。
//
// Reader 可以只负责导入扁平 Track；播放和未来的谱面渲染通过该对象共享
// 稳定的 Part/Staff/Measure/Segment/Chord/Rest 关系。
class ScorePlaybackDom final {
public:
    // 从已经定稿的 MusicDocument 构建只读 DOM。
    static std::shared_ptr<const ScorePlaybackDom> build(const MusicDocument& document);

    // 按输入轨道排列的谱面 part。
    const QVector<ScoreDomPart>& parts() const { return m_parts; }
    // 全局音符节点，id 只在本次 DOM 中稳定。
    const QVector<ScoreDomNote>& notes() const { return m_notes; }
    // 全局和弦节点。
    const QVector<ScoreDomChord>& chords() const { return m_chords; }
    // 全局休止节点。
    const QVector<ScoreDomRest>& rests() const { return m_rests; }
    // 渐强/渐弱等 spanner。
    const QVector<ScoreDomSpanner>& spanners() const { return m_spanners; }

private:
    QVector<ScoreDomPart> m_parts;
    QVector<ScoreDomNote> m_notes;
    QVector<ScoreDomChord> m_chords;
    QVector<ScoreDomRest> m_rests;
    QVector<ScoreDomSpanner> m_spanners;
};

} // namespace midi_play::music
