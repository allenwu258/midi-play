#include "midireaderadapter.h"

namespace midi_play::readers {

bool MidiReaderAdapter::canRead(const QString& suffix) const
{
    return suffix.compare(QStringLiteral("mid"), Qt::CaseInsensitive) == 0
        || suffix.compare(QStringLiteral("midi"), Qt::CaseInsensitive) == 0
        || suffix.compare(QStringLiteral("kar"), Qt::CaseInsensitive) == 0;
}

music::ReadResult MidiReaderAdapter::read(const QString& path) const
{
    return m_reader.read(path);
}

} // namespace midi_play::readers
