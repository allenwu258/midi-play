#pragma once

// 播放域与具体音频/MIDI 后端之间的能力声明和事件提交接口。
#include "playbacktypes.h"

#include <QString>

#include <atomic>
#include <memory>

namespace midi_play::playback {

// 音频后端使用的时钟来源。
enum class PlaybackClockSource {
    SoftwareMonotonic,
    AudioDevice
};

// 音频后端能力快照。在服务生命周期内保持不变，实时调度无需每个 tick
// 跨线程查询音频工作线程。
struct PlaybackBackendCapabilities {
    PlaybackClockSource clockSource = PlaybackClockSource::SoftwareMonotonic;
    bool timedEvents = false;
    bool perNoteExpression = false;
    bool metronome = false;

    bool usesAudioClock() const { return clockSource == PlaybackClockSource::AudioDevice; }
    // 仅即时发送 MIDI 时，软件时钟缩放才不会与后端预排队或设备时钟冲突。
    bool supportsSoftwarePlaybackRate() const { return !usesAudioClock() && !timedEvents; }
    bool supportsMetronome() const { return metronome && supportsSoftwarePlaybackRate(); }
};

// 音频设备后端发布帧位置的原子快照。
// 后端可在音频回调或工作线程写入，实时消费者只执行原子读取。
class PlaybackClockSnapshot final {
public:
    // 发布一个新的音频位置；release 保证消费者看到完整的值。
    void publish(qint64 positionUs) noexcept
    {
        m_positionUs.store(positionUs, std::memory_order_release);
    }

    // 使用 -1 表示当前没有可用的音频硬件位置。
    void invalidate() noexcept { publish(-1); }

    // 读取最近一次发布的位置。
    qint64 positionUs() const noexcept
    {
        return m_positionUs.load(std::memory_order_acquire);
    }

private:
    std::atomic<qint64> m_positionUs {-1};
};

class IPlaybackAudioService {
public:
    // 接口析构必须为虚函数，以便会话安全释放具体后端。
    virtual ~IPlaybackAudioService() = default;
    // 加载音源文件；失败时写入面向用户的诊断信息。
    virtual bool loadSoundFont(const QString& path, QString* error) = 0;
    // 为轨道配置通道、音色和后端所需的轨道资源。
    virtual bool configureTrack(const QString& trackId, int channel, int program, QString* error) = 0;
    // 将一条完整轨道加入后端。
    virtual bool addTrack(const PlaybackData& data, QString* error) = 0;
    // 启动音频输出。
    virtual bool start() = 0;
    // 暂停音频输出但保留后端状态。
    virtual bool pause() = 0;
    // 停止并清理音频输出。
    virtual bool stop() = 0;
    // 将后端定位到指定播放时间。
    virtual bool seek(qint64 positionUs) = 0;
    // 在后端内部设置传输位置；默认实现表示无需额外操作。
    virtual bool setTransportPosition(qint64 positionUs)
    {
        Q_UNUSED(positionUs)
        return true;
    }
    // 直连后端可在调用线程直接提供时钟位置。跨线程装饰器必须使用
    // clockSnapshot()，不能跨线程调用此虚函数。
    virtual qint64 clockPositionUs() const { return -1; }
    // AudioDevice 后端可返回预先创建的原子快照；空指针表示装饰器应退回
    // 软件时钟，避免不安全的跨线程调用。
    virtual std::shared_ptr<const PlaybackClockSnapshot> clockSnapshot() const { return {}; }
    // 声明后端采用的时钟来源。
    virtual PlaybackClockSource clockSource() const { return PlaybackClockSource::SoftwareMonotonic; }
    // 是否支持带时间戳的事件预排队。
    virtual bool supportsTimedEvents() const { return false; }
    // 是否支持单音符表达控制。
    virtual bool supportsPerNoteExpression() const { return false; }
    // 是否支持节拍器点击。
    virtual bool supportsMetronome() const { return false; }
    // 准备节拍器资源，可执行 I/O；应在音源加载后调用，不能放在实时 tick 中。
    // 此可选功能失败不应阻止普通音乐播放。
    virtual bool prepareMetronome(QString* error)
    {
        if (error) error->clear();
        return supportsMetronome();
    }
    // 汇总后端能力，供 PlaybackSession 在构造期间缓存。
    virtual PlaybackBackendCapabilities capabilities() const
    {
        return {clockSource(), supportsTimedEvents(), supportsPerNoteExpression(), supportsMetronome()};
    }
    // 立即清空后端尚未结束的事件和音符。
    virtual bool flush() = 0;
    // 提交单个主事件。
    virtual void submit(const PlaybackEvent& event) = 0;
    // 提交单个明确的 NoteOff/释放事件。
    virtual void submitOff(const PlaybackEvent& event) = 0;
    // 批量提交默认退化为逐事件提交；实时后端可覆盖以减少线程切换开销。
    virtual void submitBatch(const QVector<PlaybackEvent>& events)
    {
        for (const auto& event : events) {
            submit(event);
        }
    }
    virtual void setEventGeneration(quint64 generation)
    {
        Q_UNUSED(generation)
    }
    virtual void submitBatch(const QVector<PlaybackEvent>& events, quint64 generation)
    {
        Q_UNUSED(generation)
        submitBatch(events);
    }
    // 提交节拍器点击。generation 是传输代次，便于后端丢弃旧歌曲的点击。
    virtual void submitMetronomeClick(bool accent, quint64 generation)
    {
        Q_UNUSED(accent)
        Q_UNUSED(generation)
    }
    // 仅停止节拍器，不影响普通 MIDI 事件。
    virtual void stopMetronome() {}
    // 替换轨道的主事件流；简单后端可接受默认成功实现。
    virtual bool updateMainStream(const QString& trackId, const PlaybackEventMap& events)
    {
        Q_UNUSED(trackId)
        Q_UNUSED(events)
        return true;
    }
    // 替换轨道的 NoteOff 事件流。
    virtual bool updateOffStream(const QString& trackId, const PlaybackEventMap& events)
    {
        Q_UNUSED(trackId)
        Q_UNUSED(events)
        return true;
    }
};

} // namespace midi_play::playback
