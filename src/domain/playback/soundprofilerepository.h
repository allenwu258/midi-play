#pragma once

// 音源资源、元数据和音色预置的基础仓储接口。
#include "playbacktypes.h"

#include <QMap>

namespace midi_play::playback {

// 一组可供播放会话选择的音源、元数据和音色预置。
struct SoundProfile {
    QString name;
    QMap<QString, AudioResource> resources;
    QMap<QString, AudioResourceMeta> metadata;
    QMap<QString, QVector<SoundPreset>> presets;

    // 根据播放配置取出音频资源。
    AudioResource resourceFor(const PlaybackSetupData& setup) const
    {
        return resources.value(setup.soundId);
    }

    // 取出资源展示元数据。
    AudioResourceMeta resourceMeta(const PlaybackSetupData& setup) const
    {
        return metadata.value(setup.soundId);
    }

    // 取出指定资源的音色列表。
    QVector<SoundPreset> soundPresets(const PlaybackSetupData& setup) const
    {
        return presets.value(setup.soundId);
    }
};

class SoundProfileRepository final {
public:
    // 注册仅包含可加载路径的资源。
    void registerResource(const PlaybackSetupData& setup, const AudioResource& resource);
    // 注册资源元数据及可选的音色预置，并自动建立资源路径映射。
    void registerResource(const PlaybackSetupData& setup, const AudioResourceMeta& metadata,
                          const QVector<SoundPreset>& presets = {});
    // 返回基础 profile 中所有可展示的资源。
    QVector<AudioResourceMeta> availableResources() const;
    // 返回指定资源的可用音色预置。
    QVector<SoundPreset> availableSoundPresets(const PlaybackSetupData& setup) const;
    // 返回基础音源配置；当前仓储只维护这一默认 profile。
    const SoundProfile& basicProfile() const { return m_basic; }

private:
    SoundProfile m_basic {QStringLiteral("Basic"), {}};
};

} // namespace midi_play::playback
