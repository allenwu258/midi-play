#pragma once

#include "imusicreader.h"

#include <memory>
#include <vector>

namespace midi_play::readers {

class MusicReaderRegistry final {
public:
    void registerReader(std::unique_ptr<IMusicReader> reader) { m_readers.push_back(std::move(reader)); }
    const IMusicReader* find(const QString& suffix) const;

private:
    std::vector<std::unique_ptr<IMusicReader>> m_readers;
};

} // namespace midi_play::readers
