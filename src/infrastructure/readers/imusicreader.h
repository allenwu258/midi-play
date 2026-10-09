#pragma once

// 输入格式适配器的公共契约，统一返回音乐领域文档和读取错误。
#include "domain/music/musicreadresult.h"

#include <QString>

namespace midi_play::readers {

// 根据后缀声明可读格式；实际格式校验由具体读取器执行。
class IMusicReader {
public:
    // 允许注册表通过接口指针销毁具体适配器。
    virtual ~IMusicReader() = default;
    // 判断不带点号的扩展名是否属于本读取器支持的格式。
    virtual bool canRead(const QString& suffix) const = 0;
    // 同步读取文件；失败通过 ReadResult 返回，不向外暴露格式中间模型。
    virtual music::ReadResult read(const QString& path) const = 0;
};

} // namespace midi_play::readers
