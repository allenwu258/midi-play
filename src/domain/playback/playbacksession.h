#pragma once

// 单首乐曲播放会话的传输、调度、音源和节拍器协同接口。
#include "domain/music/musicdocument.h"
#include "playbacktypes.h"
#include "iplaybackaudioservice.h"
#include "playbackmodel.h"
#include "playbackeventscheduler.h"
#include "playbackclock.h"
#include "playbackplayhead.h"
#include "domain/settings/playersettings.h"
#include "metronometimeline.h"

#include <QObject>
#include <QTimer>
#include <memory>

namespace midi_play::playback {

// 单首乐曲的播放会话。
// 会话运行在播放线程中，负责把 PlaybackModel 的事件流按传输时钟提交给音频后端，
// 并在 play/pause/stop/seek 和音源切换时维护一致的状态。
class PlaybackSession final : public QObject {
    Q_OBJECT
public:
    // 使用不可变文档和独占音频服务创建会话。
    explicit PlaybackSession(std::shared_ptr<const music::MusicDocument> document,
                             std::unique_ptr<IPlaybackAudioService> audioService,
                             QObject* parent = nullptr);
    ~PlaybackSession() override;

    // 返回当前传输状态。
    State state() const { return m_state; }
    // 返回最近一次发布的播放位置和文档时长。
    qint64 positionMicroseconds() const { return m_positionUs; }
    qint64 durationMicroseconds() const { return m_durationUs; }
    int playbackRatePercent() const { return m_playbackRatePercent; }
    // 根据后端能力判断是否允许软件变速。
    bool supportsPlaybackRate() const { return m_audioCapabilities.supportsSoftwarePlaybackRate(); }
    bool supportsMetronome() const { return m_metronomeAvailable; }
    const QString& metronomeUnavailableReason() const { return m_metronomeUnavailableReason; }
    bool metronomeEnabled() const { return m_metronomeEnabled; }
    // 同步加载音源；播放中切换会在原位置暂停并恢复。
    bool loadSoundFont(const QString& path, QString* error);

public slots:
    // 在会话所属播放线程异步加载音源。
    void loadSoundFontAsync(const QString& path);
    // 从当前位置开始播放。
    void play();
    // 暂停并冲刷活动音符。
    void pause();
    // 停止、清理后端状态并回到曲首。
    void stop();
    // 定位到目标微秒；播放中会自动恢复播放。
    void seek(qint64 microseconds);
    // 设置播放速率百分比，非法或后端不支持时返回 false。
    bool setPlaybackRatePercent(int percent);
    // 设置节拍器偏好；实际可用性通过独立信号报告。
    void setMetronomeEnabled(bool enabled);

signals:
    void stateChanged(midi_play::playback::State state);
    void positionChanged(qint64 position, qint64 duration);
    void errorOccurred(const QString& message);
    void soundFontLoadFinished(bool success, const QString& error);
    void metronomeAvailabilityChanged(bool available, const QString& reason);

private slots:
    // 以高频精确计时器推进事件调度和位置发布。
    void onTimer();

private:
    // 应用状态变化并通知控制器/UI。
    void setState(State state);
    // 发布当前精确位置。
    void emitPosition();
    // 向音频后端发送 flush，结束当前活动音符。
    void flushActiveNotes();
    // 从最近状态快照重建目标位置的通道和音符状态。
    void rebuildAudioState(qint64 targetUs);
    // 音源切换后重新启动后端并恢复传输位置。
    bool resumeAfterSoundFontChange(qint64 positionUs, QString* error);
    // 增加传输代次，使旧异步事件失效。
    void advanceEventGeneration();
    // 启动后端、重建状态并从当前位置开始计时。
    bool startPlaybackFromCurrentPosition();
    // 从后端读取音频时钟；不可用时回退软件时钟。
    void updateAudioClockPosition();
    // 根据当前位置消费并提交节拍器点击。
    void dispatchMetronome(qint64 positionUs);
    // 在非实时路径准备节拍器资源并发布可用性。
    void prepareMetronome();

    std::shared_ptr<const music::MusicDocument> m_document;
    std::unique_ptr<IPlaybackAudioService> m_audioService;
    PlaybackModel m_playbackModel;
    const PlaybackBackendCapabilities m_audioCapabilities;
    const qint64 m_durationUs;
    QTimer* m_timer = nullptr;
    State m_state = State::Ready;
    qint64 m_positionUs = 0;
    qint64 m_clockBaseUs = 0;
    int m_playbackRatePercent = midi_play::settings::kDefaultPlaybackRatePercent;
    PlaybackPlayHead m_playHead;
    PlaybackEventScheduler m_scheduler;
    std::unique_ptr<MetronomeTimeline> m_metronomeTimeline;
    MetronomeScheduler m_metronomeScheduler;
    bool m_metronomeEnabled = false;
    bool m_metronomeAvailable = false;
    QString m_metronomeUnavailableReason;
    quint64 m_eventGeneration = 1;
};

} // namespace midi_play::playback
