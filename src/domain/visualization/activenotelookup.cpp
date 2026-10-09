#include "activenotelookup.h"

// 实现固定音域高亮查找与每帧活动道/标签去重。
#include <algorithm>

namespace midi_play::visualization {

// 复用每帧容器容量，避免热路径重复扩容。
ActiveNoteLookup::ActiveNoteLookup()
{
    m_activePitches.reserve(MidiPitchCount);
    m_melodicLabelNoteIndices.reserve(MidiPitchCount);
    reset();
}

// 将查找结果置为 -1，但保留向量容量供下一帧复用。
void ActiveNoteLookup::reset(qsizetype drumLaneCount)
{
    m_pitchNoteIndices.fill(-1);
    m_drumLaneNoteIndices.fill(-1, std::max<qsizetype>(0, drumLaneCount));
    m_activePitches.clear();
    m_activeDrumLanes.clear();
    m_activeDrumLanes.reserve(std::max<qsizetype>(0, drumLaneCount));
    m_labelPitches.reset();
    m_melodicLabelNoteIndices.clear();
}

// 旋律和鼓组使用独立查找表，不让同音高的鼓事件覆盖钢琴键。
void ActiveNoteLookup::add(int noteIndex, const VisualNote& note)
{
    if (noteIndex < 0) {
        return;
    }

    if (note.isPercussion()) {
        if (note.drumLane >= 0 && note.drumLane < m_drumLaneNoteIndices.size()) {
            if (m_drumLaneNoteIndices[note.drumLane] < 0) {
                m_activeDrumLanes.push_back(note.drumLane);
            }
            m_drumLaneNoteIndices[note.drumLane] = noteIndex;
        }
        return;
    }

    if (note.pitch < 0 || note.pitch >= MidiPitchCount) {
        return;
    }

    const auto pitchIndex = static_cast<std::size_t>(note.pitch);
    if (m_pitchNoteIndices[pitchIndex] < 0) {
        m_activePitches.push_back(note.pitch);
    }
    m_pitchNoteIndices[pitchIndex] = noteIndex;
    // 标签保留首次有效项；高亮查找表仍允许后加入音符覆盖。
    if (!note.simplifiedLabel.isEmpty() && !m_labelPitches.test(static_cast<std::size_t>(note.pitch))) {
        m_labelPitches.set(static_cast<std::size_t>(note.pitch));
        m_melodicLabelNoteIndices.push_back(noteIndex);
    }
}

// 查询不扩容、不修改状态，非法音高统一返回 -1。
int ActiveNoteLookup::noteIndexForPitch(int pitch) const
{
    return pitch >= 0 && pitch < MidiPitchCount
        ? m_pitchNoteIndices[static_cast<std::size_t>(pitch)]
        : -1;
}

// 鼓道范围由本帧 reset 时给出的数量确定。
int ActiveNoteLookup::noteIndexForDrumLane(int lane) const
{
    return lane >= 0 && lane < m_drumLaneNoteIndices.size()
        ? m_drumLaneNoteIndices[lane]
        : -1;
}

} // namespace midi_play::visualization
