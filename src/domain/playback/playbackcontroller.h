#pragma once

// UI 线程使用的播放会话控制器及跨线程位置发布接口。
#include "iplaybackaudioservice.h"
#include "playbacksession.h"
#include "playbackpositionthrottler.h"
#include "domain/settings/playersettings.h"

#include <QObject>
#include <QChronoTimer>
#include <memory>
#include <QThread>

namespace midi_play::playback {

// 播放会话的应用层控制器。
// 控制器驻留在 UI 线程，所有会话操作通过播放线程排队；位置采样通过
// PlaybackPositionThrottler 合并后按 UI 刷新频率发布。
class PlaybackController final : public QObject {
    Q_OBJECT
public:
    // 创建播放线程和位置发布定时器。
    explicit PlaybackController(QObject* parent = nullptr);
    // 停止会话、退出播放线程并等待线程安全结束。
    ~PlaybackController() override;
    // 返回当前会话；没有文档时为空。
    PlaybackSession* session() const { return m_session.get(); }
    // 返回位置发布频率和当前播放速率。
    int positionPublishRate() const noexcept { return m_positionPublishRate; }
    int playbackRatePercent() const noexcept { return m_playbackRatePercent; }

    // 替换当前文档和音频服务，创建新的播放线程会话。
    bool setDocument(std::shared_ptr<const music::MusicDocument> document,
                     std::unique_ptr<IPlaybackAudioService> audioService,
                     QString* error);
    // 同步加载音源；调用方线程会等待播放线程完成。
    bool loadSoundFont(const QString& path, QString* error);
    // 异步加载音源，不阻塞 UI 线程。
    void loadSoundFontAsync(const QString& path);
    // 更新 UI 位置发布频率。
    void setPositionPublishRate(int refreshRate);
    // 更新播放速率；后端不支持时返回 false。
    bool setPlaybackRatePercent(int percent);
    // 保存节拍器偏好并异步转发到会话。
    void setMetronomeEnabled(bool enabled);
    bool metronomeEnabled() const noexcept { return m_metronomeEnabled; }
    bool supportsMetronome() const noexcept { return m_supportsMetronome; }
    const QString& metronomeUnavailableReason() const { return m_metronomeUnavailableReason; }

public slots:
    // 将传输命令排队到播放线程。
    void play();
    void pause();
    void stop();
    void seek(qint64 microseconds);

signals:
    void stateChanged(midi_play::playback::State state);
    // 合并后的传输采样通常按控制器帧间隔发布；传输边界可立即发布端点，
    // 音频事件调度仍由 PlaybackSession 负责。
    void positionChanged(qint64 position, qint64 duration, qint64 sampledAtUs = 0);
    void errorOccurred(const QString& message);
    void soundFontLoadFinished(bool success, const QString& error);
    void metronomeAvailabilityChanged(bool available, const QString& reason);

private:
    // 消费最新位置采样并向 UI 发出一次 positionChanged。
    void flushPositionUpdate();

private slots:
    // 按配置频率触发位置采样发布。
    void onPositionTimer();

private:
    std::unique_ptr<PlaybackSession> m_session;
    QThread m_playbackThread;
    QChronoTimer m_positionTimer;
    PlaybackPositionThrottler m_positionThrottler;
    int m_positionPublishRate = 60;
    int m_playbackRatePercent = midi_play::settings::kDefaultPlaybackRatePercent;
    bool m_supportsPlaybackRate = true;
    bool m_supportsMetronome = false;
    bool m_metronomeEnabled = false;
    QString m_metronomeUnavailableReason;
};

} // namespace midi_play::playback
