#pragma once

// 同音高视觉音符的稳定横向子道分配接口。
#include "visualchart.h"

namespace midi_play::visualization {

// 输入须按开始时间排序；仅按键重叠占道，踏板尾音不挤占后续击键。
// 每个重叠连通组最多四道，超限时复用最早结束道，音符道号整段固定。
void assignNoteOverlapLanes(QVector<VisualNote>& notes);

} // namespace midi_play::visualization
