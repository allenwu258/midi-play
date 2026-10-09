#include "midireader.h"

// MIDI 导入流水线的实现，逐阶段传播错误并在成功时返回统一领域文档。
#include "mididocumentbuilder.h"
#include "midifileparser.h"
#include "midinormalizer.h"

namespace midi_play::midi {

// 各阶段只消费前一步的有效结果，避免把残缺事件流交给文档构建器。
music::ReadResult MidiReader::read(const QString& path) const
{
    const auto parsed = MidiFileParser().parse(path);
    if (!parsed.ok()) return {nullptr, parsed.error};
    const auto normalized = MidiNormalizer().normalize(*parsed.file);
    if (!normalized.ok()) return {nullptr, normalized.error};
    return MidiDocumentBuilder().build(*normalized.file);
}

} // namespace midi_play::midi
