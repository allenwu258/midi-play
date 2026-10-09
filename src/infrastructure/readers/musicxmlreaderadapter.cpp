#include "musicxmlreaderadapter.h"

// MusicXML 扩展名分派与读取委托，不在此层解释 XML 元素。
namespace midi_play::readers {

// 通用 xml 后缀只表示候选格式，读取时仍须验证 MusicXML 根结构。
bool MusicXmlReaderAdapter::canRead(const QString& suffix) const
{
    return suffix.compare(QStringLiteral("xml"), Qt::CaseInsensitive) == 0
        || suffix.compare(QStringLiteral("musicxml"), Qt::CaseInsensitive) == 0;
}

// 原样传播文档与解析错误。
music::ReadResult MusicXmlReaderAdapter::read(const QString& path) const
{
    return m_reader.read(path);
}

} // namespace midi_play::readers
