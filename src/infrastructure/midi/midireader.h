#pragma once

// 对外暴露 MIDI 文件到统一音乐文档的读取接口。
#include "domain/music/musicreadresult.h"

namespace midi_play::midi {

// 串联字节解析、事件归一化和领域文档构建的无状态读取器。
class MidiReader final {
public:
    // 同步导入 MIDI/KAR；任一阶段失败即返回该阶段错误。
    music::ReadResult read(const QString& path) const;
};

} // namespace midi_play::midi
