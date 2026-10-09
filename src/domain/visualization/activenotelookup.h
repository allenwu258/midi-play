#pragma once

// 每帧活动音符的音高/鼓道查找表，为键盘高亮与简谱标签提供下标。
#include "visualchart.h"

#include <QVector>

#include <array>
#include <bitset>
#include <cstddef>

namespace midi_play::visualization {

// 只保存音符下标，不保存后端颜色。后加入音符覆盖同键高亮，
// 简谱标签则保留每个音高首次加入的有效标签。
class ActiveNoteLookup final {
public:
    static constexpr int MidiPitchCount = 128;

    // 预留固定 MIDI 音域容量并初始化空查找表。
    ActiveNoteLookup();

    // 清空帧状态并按鼓道数调整查找表。
    void reset(qsizetype drumLaneCount = 0);
    // 加入一个已确认活动的音符，忽略无效下标/音高/鼓道。
    void add(int noteIndex, const VisualNote& note);

    // 返回旋律键的代表音符，无活动音符或越界时返回 -1。
    int noteIndexForPitch(int pitch) const;
    // 返回鼓道的代表音符，无活动音符或越界时返回 -1。
    int noteIndexForDrumLane(int lane) const;
    // 活动键和鼓道各只出现一次，避免每帧扫描全部音域。
    const QVector<int>& activePitches() const { return m_activePitches; }
    const QVector<int>& activeDrumLanes() const { return m_activeDrumLanes; }
    // 返回按音高去重后的有效简谱标签音符。
    const QVector<int>& melodicLabelNoteIndices() const { return m_melodicLabelNoteIndices; }

private:
    std::array<int, MidiPitchCount> m_pitchNoteIndices;
    QVector<int> m_drumLaneNoteIndices;
    QVector<int> m_activePitches;
    QVector<int> m_activeDrumLanes;
    std::bitset<MidiPitchCount> m_labelPitches;
    QVector<int> m_melodicLabelNoteIndices;
};

} // namespace midi_play::visualization
