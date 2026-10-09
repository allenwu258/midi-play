#pragma once

// 主窗口声明：组合播放可视化、传输控件、设置窗口和后台导出流程。

#include "domain/playback/playbacktypes.h"
#include "domain/settings/titlebarmode.h"
#include "domain/settings/thememode.h"
#include "domain/visualization/visualchart.h"
#include "playbackmetadatapresenter.h"

#include <QMainWindow>
#include <QPointer>
#include <atomic>
#include <memory>

class QLabel;
class QFrame;
class QSlider;
class QToolButton;
class QResizeEvent;
class QEvent;
class QProgressDialog;

namespace midi_play::app { class PlayerApplicationService; }
namespace midi_play::app { class SettingsService; }
namespace midi_play::app { class FfmpegService; }
namespace midi_play::app { struct AudioExportOptions; }
namespace midi_play::app { struct VideoExportOptions; }
namespace midi_play::presentation::settings { class SettingsDialog; }
namespace midi_play::presentation::windowchrome { class CustomTitleBar; }
namespace midi_play::presentation::windowchrome { class WindowChromeController; }
namespace midi_play::presentation::windowchrome { class CaptionButtons; }
namespace midi_play::presentation::visualization { class FallingNotesView; }
namespace midi_play::presentation::theme { class ThemeController; }

namespace midi_play::presentation {
class PlaybackRateControl;

// 展示层协调器：消费应用服务信号并转发用户命令，不负责解析或音频调度。
class MainWindow final : public QMainWindow {
    Q_OBJECT
public:
    // 服务由调用方管理，界面子控件由 Qt 父子关系管理。
    explicit MainWindow(app::PlayerApplicationService* service,
                        app::SettingsService* settingsService,
                        QWidget* parent = nullptr,
                        theme::ThemeController* themeController = nullptr,
                        app::FfmpegService* ffmpegService = nullptr);
    // 请求取消仍在执行的导出；后台任务持有自己的参数和文档快照。
    ~MainWindow() override;

protected:
    // 同步窄窗口布局、窗口状态、激活外观，并委托原生消息给窗口装饰控制器。
    void resizeEvent(QResizeEvent* event) override;
    void changeEvent(QEvent* event) override;
    bool nativeEvent(const QByteArray& eventType, void* message, qintptr* result) override;

private slots:
    // 选择乐曲并交给应用服务异步导入。
    void openMusicFile();
    // 快照当前场景与导出能力，接受参数后分派音频或视频任务。
    void openExportDialog();
    // 懒创建并复用非模态设置窗口。
    void showSettings();
    // 接收音乐位置及采样时刻；拖动和 seek 待确认期间维持用户预览。
    void updatePosition(qint64 position, qint64 duration, qint64 sampledAtUs = 0);
    // 将领域播放状态映射到状态文字、可视化状态和按钮可用性。
    void updatePlaybackState(midi_play::playback::State state);

private:
    // 将非负微秒截断为秒，按曲长输出 mm:ss 或 h:mm:ss。
    static QString formatTime(qint64 microseconds);
    // 缓存秒级显示值，仅在可见时间发生变化时更新标签。
    void updateTimeDisplay(qint64 positionUs, qint64 durationUs);
    // 仅在元数据缓存切换区间时更新调号、拍号及原曲 BPM。
    void updateMetadata(qint64 positionUs);
    // 窄窗口隐藏元数据，为文件名和操作按钮保留空间。
    void updateResponsiveVisibility();
    // 依据文档、播放状态和音源加载状态更新传输控件。
    void updateTransportControls();
    // 应用标题栏模式并刷新有效窗口装饰。
    void applyTitleBarMode(midi_play::settings::TitleBarMode mode);
    // 按有效模式、全屏和 DPI 度量调整标题栏及系统按钮。
    void updateWindowChrome();
    // 同步窗口、原生装饰、可视化、倍率面板和按钮图标的主题。
    void applyTheme(midi_play::settings::ThemeMode mode);
    // 有文档且没有导出任务时开放导出入口，并显示禁用原因。
    void updateExportButtonState();
    // 捕获文档及音频参数，在工作线程导出；GUI 定时读取原子进度并处理完成结果。
    void startAudioExport(const app::AudioExportOptions& options);
    // Vulkan 构建中启动视频任务，使用独立文档与场景参数快照。
    void startVideoExport(const app::VideoExportOptions& options);

    // 固定整数刻度与微秒曲长按比例映射，避免滑块范围随曲长增长。
    static constexpr int kSliderResolution = 1'000'000;

    app::PlayerApplicationService* m_service = nullptr;
    app::SettingsService* m_settingsService = nullptr;
    app::FfmpegService* m_ffmpegService = nullptr;
    theme::ThemeController* m_themeController = nullptr;
    midi_play::settings::ThemeMode m_themeMode = midi_play::settings::kDefaultThemeMode;
    QPointer<settings::SettingsDialog> m_settingsDialog;
    windowchrome::CustomTitleBar* m_topBar = nullptr;
    windowchrome::WindowChromeController* m_windowChrome = nullptr;
    QToolButton* m_systemMenuButton = nullptr;
    windowchrome::CaptionButtons* m_captionButtons = nullptr;
    visualization::FallingNotesView* m_visualization = nullptr;
    QLabel* m_fileLabel = nullptr;
    QLabel* m_keyLabel = nullptr;
    QLabel* m_timeSignatureLabel = nullptr;
    QLabel* m_tempoLabel = nullptr;
    QLabel* m_timeLabel = nullptr;
    QLabel* m_statusLabel = nullptr;
    QLabel* m_soundFontErrorLabel = nullptr;
    QToolButton* m_playButton = nullptr;
    QToolButton* m_openButton = nullptr;
    QToolButton* m_exportButton = nullptr;
    QToolButton* m_settingsButton = nullptr;
    QToolButton* m_pauseButton = nullptr;
    QToolButton* m_stopButton = nullptr;
    QToolButton* m_metronomeButton = nullptr;
    PlaybackRateControl* m_playbackRateControl = nullptr;
    QSlider* m_positionSlider = nullptr;
    // 界面持有只读图表；元数据缓存与可视化使用同一音乐时间轴。
    midi_play::visualization::VisualChartPtr m_chart;
    PlaybackMetadataTimeline m_metadataTimeline;
    playback::State m_playbackState = playback::State::Empty;
    qint64 m_positionUs = 0;
    qint64 m_durationUs = 0;
    bool m_sliderDragging = false;
    // 单个导出任务的界面状态；取消标志与后台任务共享，进度窗口用弱指针追踪。
    bool m_exporting = false;
    std::shared_ptr<std::atomic_bool> m_exportCancel;
    QPointer<QProgressDialog> m_exportProgress;
    // 等待 seek 的精确位置确认，避免排队的旧位置覆盖松开滑块时的展示。
    bool m_seekPending = false;
    qint64 m_pendingSeekUs = 0;
    qint64 m_displayedPositionSecond = -1;
    qint64 m_displayedDurationSecond = -1;
};

} // namespace midi_play::presentation
