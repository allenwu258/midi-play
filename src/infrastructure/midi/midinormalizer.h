#pragma once

// 将 MIDI 原始事件转换为统一时间基准、逻辑轨道和有完整生命周期的音符。
#include "miditypes.h"

namespace midi_play::midi {

// 归一化文件、容错诊断和失败信息的集合。
struct MidiNormalizeResult {
    std::shared_ptr<MidiNormalizedFile> file;
    MidiReadDiagnostics diagnostics;
    QString error;
    // 诊断计数可非零；有效文件且无错误即视为成功。
    bool ok() const { return file && error.isEmpty(); }
};

// 负责通道分组、音符配对、踏板保持及 format 2 序列拼接。
class MidiNormalizer final {
public:
    // 生成文档构建所需数据，保留源文件信息但不修改输入。
    MidiNormalizeResult normalize(const MidiParsedFile& source) const;
    // 按源 division 缩放到固定 kPpq，使用高精度中间值并四舍五入。
    static music::Tick scaleTick(music::Tick sourceTick, const MidiFileHeader& header);
};

} // namespace midi_play::midi
