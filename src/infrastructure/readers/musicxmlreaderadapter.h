#pragma once

#include "imusicreader.h"

namespace midi_play::readers {

class MusicXmlReaderAdapter final : public IMusicReader {
public:
    bool canRead(const QString& suffix) const override;
    musicxml::ReadResult read(const QString& path) const override;

private:
    musicxml::MusicXmlReader m_reader;
};

} // namespace midi_play::readers
