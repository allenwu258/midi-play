#pragma once

// MusicXML 到统一音乐文档的同步导入接口，支持未压缩的 partwise/timewise XML。
#include "domain/music/musicdocument.h"
#include "domain/music/musicreadresult.h"

#include <QString>

namespace midi_play::musicxml {

// 复用领域读取结果，保持与 MIDI 导入相同的成功/失败契约。
using ReadResult = music::ReadResult;

// 提取播放和可视化所需的音乐语义，不构建完整的谱面排版模型。
class MusicXmlReader final {
public:
    // 读取乐器、小节、音符、方向和反复标记，完成时间索引及领域分析。
    ReadResult read(const QString& path) const;
};

} // namespace midi_play::musicxml
