#include "musicreaderregistry.h"

namespace midi_play::readers {

const IMusicReader* MusicReaderRegistry::find(const QString& suffix) const
{
    for (const auto& reader : m_readers) if (reader->canRead(suffix)) return reader.get();
    return nullptr;
}

} // namespace midi_play::readers
