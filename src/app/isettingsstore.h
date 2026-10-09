#pragma once
// 设置持久化端口：应用服务通过此接口读取和保存值对象。

#include "domain/settings/playersettings.h"

#include <QString>

namespace midi_play::app {

// 实现负责存储格式及错误转换，应用层不依赖 QSettings。
class ISettingsStore {
public:
    virtual ~ISettingsStore() = default;

    // 返回读取后的设置，warning 用于报告回退或字段修复。
    virtual settings::PlayerSettings load(QString* warning) = 0;
    // 返回是否写入成功，失败信息通过 error 传回调用方。
    virtual bool save(const settings::PlayerSettings& settings, QString* error) = 0;
};

} // namespace midi_play::app
