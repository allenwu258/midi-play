#pragma once

#include "musicdocument.h"

#include <QString>
#include <memory>

// Reader 到应用层的统一文档读取结果接口。
namespace midi_play::music {

// Reader 的统一返回值。
//
// 成功时 document 非空且 error 为空；失败时 document 通常为空，error
// 保存可直接向应用层传播的诊断文本。
struct ReadResult {
    std::shared_ptr<MusicDocument> document;
    QString error;

    bool ok() const { return document && error.isEmpty(); }
};

} // namespace midi_play::music
