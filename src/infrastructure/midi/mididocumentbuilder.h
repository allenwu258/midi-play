#pragma once

// MIDI 归一化数据到音乐领域文档的转换边界。
#include "miditypes.h"
#include "domain/music/musicreadresult.h"

namespace midi_play::midi {

// 生成音乐轨道、控制变化和元数据，并完成领域时间索引与分析。
class MidiDocumentBuilder final {
public:
    // 构建可播放文档；无有效音符时返回读取失败。
    music::ReadResult build(const MidiNormalizedFile& source) const;
};

} // namespace midi_play::midi
