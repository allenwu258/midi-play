#include "musicreaderregistry.h"

// 实现格式注册表的后缀查询，生命周期由注册表管理。
namespace midi_play::readers {

// 返回第一个支持该后缀的读取器，保持注册顺序所表达的优先级。
const IMusicReader* MusicReaderRegistry::find(const QString& suffix) const
{
    for (const auto& reader : m_readers) if (reader->canRead(suffix)) return reader.get();
    return nullptr;
}

} // namespace midi_play::readers
