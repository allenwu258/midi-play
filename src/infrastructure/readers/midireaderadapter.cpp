#include "midireaderadapter.h"

// MIDI 格式适配器实现；KAR 复用相同的 SMF 解析链路。
namespace midi_play::readers {

// 这里只做扩展名分派，文件内容的合法性由底层解析器检查。
bool MidiReaderAdapter::canRead(const QString& suffix) const
{
    return suffix.compare(QStringLiteral("mid"), Qt::CaseInsensitive) == 0
        || suffix.compare(QStringLiteral("midi"), Qt::CaseInsensitive) == 0
        || suffix.compare(QStringLiteral("kar"), Qt::CaseInsensitive) == 0;
}

// 保留底层读取器的文档和错误信息，不作二次转换。
music::ReadResult MidiReaderAdapter::read(const QString& path) const
{
    return m_reader.read(path);
}

} // namespace midi_play::readers
