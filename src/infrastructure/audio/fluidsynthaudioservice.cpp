#include "fluidsynthaudioservice.h"

// FluidSynth 播放适配器实现，将领域控制和事件转交给引擎。
namespace midi_play::audio {

// 保持引擎独占所有权，其生命周期覆盖本服务的所有委托调用。
FluidSynthAudioService::FluidSynthAudioService(std::unique_ptr<FluidSynthEngine> engine)
    : m_engine(std::move(engine))
{
}

// 复用引擎的首次初始化和音源热切换逻辑。
bool FluidSynthAudioService::loadSoundFont(const QString& path, QString* error)
{
    return m_engine->load(path, error);
}
// 轨道 ID 不参与底层选择，配置由通道与音色确定。
bool FluidSynthAudioService::configureTrack(const QString&, int channel, int program, QString* error) { return m_engine->configureTrack(channel, program, error); }
// 只初始化音色，不把事件交给 FluidSynth 持有或自动调度。
bool FluidSynthAudioService::addTrack(const playback::PlaybackData& data, QString* error)
{
    return configureTrack(data.trackId, data.events.isEmpty() ? 0 : data.events.front().channel,
                          data.events.isEmpty() ? 0 : data.events.front().program, error);
}
// 以下传输方法委托引擎；会话始终是时间推进和 seek 状态恢复的负责人。
bool FluidSynthAudioService::start()
{
    return m_engine->start();
}
bool FluidSynthAudioService::pause() { return m_engine->pause(); }
bool FluidSynthAudioService::stop() { return m_engine->stop(); }
bool FluidSynthAudioService::seek(qint64 positionUs) { return m_engine->seek(positionUs); }
bool FluidSynthAudioService::setTransportPosition(qint64 positionUs) { return m_engine->setTransportPosition(positionUs); }
qint64 FluidSynthAudioService::clockPositionUs() const { return m_engine->clockPositionUs(); }
// 原生驱动输出音频，但当前引擎没有向调度层发布设备帧位置。
playback::PlaybackClockSource FluidSynthAudioService::clockSource() const
{
    return playback::PlaybackClockSource::SoftwareMonotonic;
}
bool FluidSynthAudioService::supportsTimedEvents() const { return false; }
bool FluidSynthAudioService::supportsPerNoteExpression() const { return false; }
// 在音源加载前声明节拍器契约，供会话缓存能力；prepareMetronome 再确认实际就绪。
bool FluidSynthAudioService::supportsMetronome() const { return true; }
bool FluidSynthAudioService::prepareMetronome(QString* error) { return m_engine->prepareMetronome(error); }
bool FluidSynthAudioService::flush() { return m_engine->flush(); }
// 主事件和释放事件都立即执行，不读取其时间戳进行二次调度。
void FluidSynthAudioService::submit(const playback::PlaybackEvent& event)
{
    m_engine->submit(event);
}

// 保留事件类型，使 NoteOff 的通道和音高原样进入引擎。
void FluidSynthAudioService::submitOff(const playback::PlaybackEvent& event)
{
    m_engine->submit(event);
}

// 此直连服务没有异步队列，generation 由外层负责过滤。
void FluidSynthAudioService::submitMetronomeClick(bool accent, quint64)
{
    m_engine->submitMetronomeClick(accent);
}

// 停止点击不执行歌曲合成器重置。
void FluidSynthAudioService::stopMetronome()
{
    m_engine->stopMetronome();
}

} // namespace midi_play::audio
