#pragma once

// 为通用格式注册表提供 MIDI/KAR 读取能力。
#include "imusicreader.h"
#include "infrastructure/midi/midireader.h"

namespace midi_play::readers {

// 将后缀匹配与 MIDI 导入流水线接入 IMusicReader 契约。
class MidiReaderAdapter final : public IMusicReader {
public:
    // 大小写无关地匹配 mid、midi 和 kar。
    bool canRead(const QString& suffix) const override;
    // 委托 MidiReader 完成解析、归一化和文档构建。
    music::ReadResult read(const QString& path) const override;

private:
    midi::MidiReader m_reader;
};

} // namespace midi_play::readers
