#pragma once
// 播放应用编排入口：连接异步导入、音源选择、播放控制与 GUI 状态发布。

#include "domain/playback/playbackcontroller.h"
#include "domain/settings/playersettings.h"
#include "domain/visualization/visualchart.h"
#include "infrastructure/readers/musicreaderregistry.h"

#include <QObject>
#include <QFutureWatcher>
#include <memory>

namespace midi_play::app {

// 在 GUI 线程持有当前会话与只读文档，具体解析/音频操作委托相应模块。
class PlayerApplicationService final : public QObject {
    Q_OBJECT
public:
    // 注册文件读取器和 Qt 元类型，建立无会话时的异步音源验证回调。
    explicit PlayerApplicationService(QObject* parent = nullptr);

    // 会话为借用指针，打开新乐曲成功后会被替换；文档以只读共享所有权返回。
    playback::PlaybackSession* session() const { return m_controller ? m_controller->session() : nullptr; }
    QString fileName() const { return m_fileName; }
    std::shared_ptr<const music::MusicDocument> document() const { return m_document; }
    QString soundFontPath() const { return m_soundFontPath; }
    playback::State playbackState() const { return m_playbackState; }
    qint64 positionMicroseconds() const { return m_positionUs; }
    qint64 durationMicroseconds() const { return m_durationUs; }
    // 当前播放偏好与能力查询，未载入乐曲时节拍器能力不可用。
    int visualizationRefreshRate() const noexcept { return m_visualizationRefreshRate; }
    int playbackRatePercent() const noexcept { return m_playbackRatePercent; }
    bool metronomeEnabled() const noexcept { return m_metronomeEnabled; }
    bool supportsMetronome() const noexcept { return m_controller && m_controller->supportsMetronome(); }
    QString metronomeUnavailableReason() const
    {
        return m_controller ? m_controller->metronomeUnavailableReason() : QStringLiteral("请先加载乐曲");
    }
    // 路径非空只表示已选择音源，播放前仍会重新检查外部文件。
    bool hasSoundFont() const noexcept { return !m_soundFontPath.isEmpty(); }
    bool isSoundFontLoading() const noexcept { return m_soundFontLoading; }
    const QString& lastSoundFontError() const noexcept { return m_lastSoundFontError; }

public slots:
    // 按后缀选择读取器，异步解析/投影；仅最新代次成功结果可以替换当前会话。
    void openFile(const QString& path);
    // 保留 MusicXML 专用调用入口，实际复用通用打开流程。
    void openMusicXml(const QString& path);
    // 同步检查/加载音源，切换失败时尝试恢复先前音源。
    bool loadSoundFont(const QString& path);
    // GUI 使用的异步入口：已有会话由控制器加载，否则后台验证音源。
    void requestSoundFontLoad(const QString& path);
    // 更新位置发布频率，不改变音频事件调度周期。
    void setVisualizationRefreshRate(int refreshRate);
    // 夹取倍率后交给控制器；控制器拒绝时重新通知原倍率。
    void setPlaybackRatePercent(int percent);
    // 保存节拍器偏好并传递给已有控制器。
    void setMetronomeEnabled(bool enabled);
    // 规范化并广播图形模式，实际资源创建和回退由视图处理。
    void setGraphicsMode(settings::GraphicsMode mode);
    settings::GraphicsMode graphicsMode() const noexcept { return m_graphicsMode; }
    // 播放前检查音源；暂停/停止只转发到已存在的控制器。
    void play();
    void pause();
    void stop();
    // 请求控制器跳转，并以当前缓存位置发出视觉不连续通知。
    void seek(qint64 microseconds);

signals:
    // 文档导入的忙碌状态及成功载入元数据。
    void busyChanged(bool busy);
    void documentLoaded(const QString& title, qint64 duration);
    // 音源成功后通知显示层；SelectionCommitted 是设置持久化的提交点。
    void soundFontLoaded(const QString& path);
    void soundFontSelectionCommitted(const QString& path);
    void soundFontLoadFailed(const QString& message);
    void soundFontLoadingChanged(bool loading);
    // 异步音源请求的最终结果，播放时的音源诊断不发出此信号。
    void soundFontLoadFinished(bool success);
    // 位置与采样时刻均为微秒，sampledAtUs 为零时表示精确位置而非可外推样本。
    void positionChanged(qint64 position, qint64 duration, qint64 sampledAtUs = 0);
    // seek 等不连续操作提示视图重置瞬态效果。
    void playbackDiscontinuity(qint64 position);
    void playbackStateChanged(midi_play::playback::State state);
    // 已完成投影的只读图表，可由视图直接共享。
    void visualizationReady(midi_play::visualization::VisualChartPtr chart);
    void graphicsModeChanged(midi_play::settings::GraphicsMode mode);
    void playbackRateChanged(int percent);
    void metronomeChanged(bool enabled);
    void metronomeAvailabilityChanged(bool available, const QString& reason);
    void errorOccurred(const QString& message);

private:
    // 将新控制器的状态、错误、位置和音源完成信号接入应用层。
    void connectSession();
    // 消费待定音源路径，结束忙碌状态，仅成功时发布可持久化选择。
    void completeSoundFontLoad(bool success, const QString& error);
    // 状态实际变化时才通知 GUI。
    void setSoundFontLoading(bool loading);
    // 检查路径、文件可读性和扩展名，成功时返回绝对路径。
    bool normalizeSoundFontPath(const QString& path, QString* normalizedPath);
    // 在路径预检基础上检查 RIFF 容器，不等价于完整的合成器加载。
    bool validateSoundFontFile(const QString& path, QString* normalizedPath);
    // 记录可重用错误并发出音源诊断信号。
    void reportSoundFontFailure(const QString& message);

    std::unique_ptr<playback::PlaybackController> m_controller;
    std::shared_ptr<const music::MusicDocument> m_document;
    QString m_fileName;
    QString m_soundFontPath;
    QString m_pendingSoundFontPath; // 异步加载未完成前不写入已提交音源路径。
    QString m_lastSoundFontError;
    QFutureWatcher<QString> m_soundFontValidationWatcher;
    bool m_soundFontLoading = false;
    bool m_documentLoading = false;
    readers::MusicReaderRegistry m_readerRegistry;
    playback::State m_playbackState = playback::State::Empty;
    qint64 m_positionUs = 0;
    qint64 m_durationUs = 0;
    quint64 m_loadGeneration = 0; // 丢弃旧任务结果，不主动中断后台解析。
    int m_visualizationRefreshRate = settings::kDefaultVisualizationRefreshRate;
    int m_playbackRatePercent = settings::kDefaultPlaybackRatePercent;
    bool m_metronomeEnabled = false;
    settings::GraphicsMode m_graphicsMode = settings::kDefaultGraphicsMode;
};

} // namespace midi_play::app
