#pragma once

#include "musicdocument.h"

// 从统一音乐文档生成共享的小节细分网格和节拍器点击网格。
namespace midi_play::music {

// 节奏网格上的一个离散点。
struct RhythmPoint {
    Tick tick = 0;
    int measureIndex = -1;
    int measureNumber = 0;
    int beatIndex = 0;
    bool downbeat = false;
};

// 反复展开前的源乐谱节奏网格。
//
// 可视化小节线和节拍器点击共同消费同一份拍号、小节边界和弱起相位。
// subdivisions 使用记谱分母，clicks 使用显式节拍单位、MIDI clocks 或
// 拍号分组，不能由两个消费者分别根据 PPQ 猜测小节。
class MusicRhythmGrid final {
public:
    // 从文档构建不可变的节奏和点击网格。
    explicit MusicRhythmGrid(const MusicDocument& document);

    // 用于绘制的小节/拍内细分点。
    const QVector<RhythmPoint>& subdivisions() const { return m_subdivisions; }
    // 用于节拍器调度的点击点。
    const QVector<RhythmPoint>& clicks() const { return m_clicks; }
    // 规范化后的拍号变化。
    const QVector<TimeSignatureChange>& signatures() const { return m_signatures; }
    // 文档具备可用节奏网格时返回 true。
    bool available() const { return m_unavailableReason.isEmpty(); }
    // 不可用时的面向用户诊断文本。
    const QString& unavailableReason() const { return m_unavailableReason; }

private:
    QVector<RhythmPoint> m_subdivisions;
    QVector<RhythmPoint> m_clicks;
    QVector<TimeSignatureChange> m_signatures;
    QString m_unavailableReason;
};

} // namespace midi_play::music
