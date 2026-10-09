#pragma once
// 使用用户级 INI 文件实现应用设置存储端口。

#include "app/isettingsstore.h"

#include <QString>

namespace midi_play::infrastructure::settings {

// 处理缺省值、字段校验和目录创建，运行时元信息不作为独立配置项保存。
class QSettingsStore final : public app::ISettingsStore {
public:
    // 空路径采用 AppLocalDataLocation/settings.ini，可指定路径供测试隔离。
    explicit QSettingsStore(QString settingsPath = {});

    // 读取并规范化字段，向可选 warning 聚合已检测到的配置问题。
    midi_play::settings::PlayerSettings load(QString* warning) override;
    // 创建父目录、写入当前 schema 并同步，失败时填写可选 error。
    bool save(const midi_play::settings::PlayerSettings& settings, QString* error) override;

    // 返回实际存储路径的借用引用。
    const QString& settingsPath() const noexcept { return m_settingsPath; }

    // 无可写用户数据目录时返回空路径，由读写入口处理失败。
    static QString defaultSettingsPath();

private:
    QString m_settingsPath;
};

} // namespace midi_play::infrastructure::settings
