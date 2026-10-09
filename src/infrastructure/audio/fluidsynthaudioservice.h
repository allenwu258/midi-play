#pragma once

// 将播放域音频接口适配到具体 FluidSynth 引擎。
#include "domain/playback/iplaybackaudioservice.h"
#include "fluidsynthengine.h"

#include <memory>

namespace midi_play::audio {

// 即时 MIDI 后端；不存储轨道事件流，事件调度与传输时间由播放会话维护。
class FluidSynthAudioService final : public playback::IPlaybackAudioService {
public:
    // 接管非空引擎；跨线程串行访问由外层装饰器提供。
    explicit FluidSynthAudioService(std::unique_ptr<FluidSynthEngine> engine);

    // 加载或热切换 SoundFont，原样返回引擎错误。
    bool loadSoundFont(const QString& path, QString* error) override;
    // 按通道配置初始音色；FluidSynth 无需保存逻辑轨道 ID。
    bool configureTrack(const QString& trackId, int channel, int program, QString* error) override;
    // 从轨道首事件取得初始通道/音色；空轨道使用零值。
    bool addTrack(const playback::PlaybackData& data, QString* error) override;
    // 检查引擎就绪，音频驱动已在加载阶段启动。
    bool start() override;
    // 委托暂停契约；会话负责另行 flush 活动音符。
    bool pause() override;
    // 重置引擎音符和通道状态。
    bool stop() override;
    // 清理合成状态，定位与事件重放由会话处理。
    bool seek(qint64 positionUs) override;
    // 即时后端没有传输队列，此调用只检查就绪。
    bool setTransportPosition(qint64 positionUs) override;
    // 当前返回 -1，表示没有可消费的设备时钟。
    qint64 clockPositionUs() const override;
    // 声明使用软件单调时钟。
    playback::PlaybackClockSource clockSource() const override;
    // 不支持时间戳预排队，事件需到期后提交。
    bool supportsTimedEvents() const override;
    // 当前以通道控制近似表达，不声明真正的单音符表达支持。
    bool supportsPerNoteExpression() const override;
    // 声明节拍器能力契约；实际资源就绪情况由 prepareMetronome 报告。
    bool supportsMetronome() const override;
    // 加载内置点击资源并配置独立合成器。
    bool prepareMetronome(QString* error) override;
    // 清理歌曲及点击状态，为状态重建准备后端。
    bool flush() override;
    // 即时分派普通事件。
    void submit(const playback::PlaybackEvent& event) override;
    // 释放事件复用相同引擎分派路径，事件 kind 决定具体操作。
    void submitOff(const playback::PlaybackEvent& event) override;
    // 立即触发点击；代次校验由线程装饰器在执行前完成。
    void submitMetronomeClick(bool accent, quint64 generation) override;
    // 仅停止独立点击声部。
    void stopMetronome() override;

private:
    std::unique_ptr<FluidSynthEngine> m_engine;
};

} // namespace midi_play::audio
