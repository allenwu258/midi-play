#include "musicxmlreaderadapter.h"

namespace midi_play::readers {

bool MusicXmlReaderAdapter::canRead(const QString& suffix) const
{
    return suffix.compare(QStringLiteral("xml"), Qt::CaseInsensitive) == 0
        || suffix.compare(QStringLiteral("musicxml"), Qt::CaseInsensitive) == 0;
}

musicxml::ReadResult MusicXmlReaderAdapter::read(const QString& path) const
{
    return m_reader.read(path);
}

} // namespace midi_play::readers
