#pragma once

// 标准 MIDI 文件的字节解析入口；此阶段不构建领域音符或踏板状态。
#include "miditypes.h"

namespace midi_play::midi {

// 解析结果与错误信息；失败时不向下游提供可用的中间文件。
struct MidiParseResult {
    std::shared_ptr<MidiParsedFile> file;
    QString error;
    // 只有中间文件存在且没有错误时才允许继续归一化。
    bool ok() const { return file && error.isEmpty(); }
};

// 读取 SMF 头和轨道块，处理 VLQ、Running Status、通道消息及元事件。
class MidiFileParser final {
public:
    // 读取文件并校验消息边界；失败返回具体格式或 I/O 错误。
    MidiParseResult parse(const QString& path) const;
};

} // namespace midi_play::midi
