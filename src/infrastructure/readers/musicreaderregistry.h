#pragma once

// 维护输入格式适配器的所有权及按注册顺序分派的规则。
#include "imusicreader.h"

#include <memory>
#include <vector>

namespace midi_play::readers {

// 应用启动时注册读取器，打开文件时按后缀查询。
class MusicReaderRegistry final {
public:
    // 转移非空读取器的所有权；较早注册者在后缀冲突时优先。
    void registerReader(std::unique_ptr<IMusicReader> reader) { m_readers.push_back(std::move(reader)); }
    // 返回注册表持有的借用指针；无匹配时返回 nullptr。
    const IMusicReader* find(const QString& suffix) const;

private:
    std::vector<std::unique_ptr<IMusicReader>> m_readers;
};

} // namespace midi_play::readers
