#pragma once

// 项目级音源配置与轨道混音参数的 JSON 存取模型。
#include "domain/playback/playbacktypes.h"

#include <QMap>
#include <QString>

namespace midi_play::resources {

// 一个轨道的资源与混音偏好；本对象只保存参数，不直接驱动音频后端。
struct TrackAudioSettings {
    QString trackId;
    playback::AudioResource resource;
    int volume = 100;
    int pan = 64;
    bool mute = false;
    bool solo = false;
};

// 保存活动音源 profile 和按 trackId 索引的轨道配置。
class ProjectAudioSettings final {
public:
    // 设置当前项目选用的音源 profile 名称。
    void setActiveSoundProfile(const QString& name) { m_activeProfile = name; }
    // 返回 profile 名称；新建对象默认使用 Basic。
    QString activeSoundProfile() const { return m_activeProfile; }
    // 以轨道 ID 覆盖对应配置。
    void setTrack(const TrackAudioSettings& settings) { m_tracks.insert(settings.trackId, settings); }
    // 未找到轨道时返回默认参数对象。
    TrackAudioSettings track(const QString& id) const { return m_tracks.value(id); }

    // 从 JSON 替换内存配置；缺失或类型不符的字段使用默认值。
    bool read(const QString& path, QString* error);
    // 将资源路径与混音偏好写为 JSON，不保存运行时 SoundFont ID。
    bool write(const QString& path, QString* error) const;

private:
    QString m_activeProfile = QStringLiteral("Basic");
    QMap<QString, TrackAudioSettings> m_tracks;
};

} // namespace midi_play::resources
