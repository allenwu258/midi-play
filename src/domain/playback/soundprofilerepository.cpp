#include "soundprofilerepository.h"

// 实现基础音源 profile 的注册、查询和资源元数据导出。
namespace midi_play::playback {

// 将简单资源绑定到 soundId；重复注册时覆盖同一资源键。
void SoundProfileRepository::registerResource(const PlaybackSetupData& setup, const AudioResource& resource)
{
    m_basic.resources.insert(setup.soundId, resource);
}

// 保存展示元数据和预置，并用 metadata.path 创建后端可加载的资源记录。
void SoundProfileRepository::registerResource(const PlaybackSetupData& setup,
                                               const AudioResourceMeta& metadata,
                                               const QVector<SoundPreset>& presets)
{
    m_basic.metadata.insert(setup.soundId, metadata);
    m_basic.presets.insert(setup.soundId, presets);
    m_basic.resources.insert(setup.soundId, AudioResource {metadata.path, -1});
}

// 按元数据键遍历基础 profile，生成调用方可安全持有的值拷贝。
QVector<AudioResourceMeta> SoundProfileRepository::availableResources() const
{
    QVector<AudioResourceMeta> result;
    result.reserve(m_basic.metadata.size());
    for (auto it = m_basic.metadata.cbegin(); it != m_basic.metadata.cend(); ++it) {
        result.push_back(it.value());
    }
    return result;
}

// 资源不存在时返回空列表，由调用方决定是否回退默认音色。
QVector<SoundPreset> SoundProfileRepository::availableSoundPresets(const PlaybackSetupData& setup) const
{
    return m_basic.soundPresets(setup);
}

} // namespace midi_play::playback
