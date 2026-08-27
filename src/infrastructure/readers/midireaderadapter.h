#pragma once

#include "imusicreader.h"
#include "infrastructure/midi/midireader.h"

namespace midi_play::readers {

class MidiReaderAdapter final : public IMusicReader {
public:
    bool canRead(const QString& suffix) const override;
    music::ReadResult read(const QString& path) const override;

private:
    midi::MidiReader m_reader;
};

} // namespace midi_play::readers
