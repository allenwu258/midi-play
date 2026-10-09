#pragma once

// 为通用格式注册表提供未压缩 MusicXML 读取能力。
#include "imusicreader.h"
#include "infrastructure/musicxml/musicxmlreader.h"

namespace midi_play::readers {

// 将 MusicXmlReader 接入统一读取接口。
class MusicXmlReaderAdapter final : public IMusicReader {
public:
    // 大小写无关地匹配 xml 和 musicxml；当前不处理压缩 mxl。
    bool canRead(const QString& suffix) const override;
    // 委托 MusicXmlReader 构建领域文档。
    music::ReadResult read(const QString& path) const override;

private:
    midi_play::musicxml::MusicXmlReader m_reader;
};

} // namespace midi_play::readers
