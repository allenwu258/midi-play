#pragma once

// 实时 Vulkan 窗口：保存宿主同步的场景设置，通过 Qt 管理设备与交换链生命周期。

#include "domain/playback/playbacktypes.h"
#include "domain/visualization/visualchart.h"
#include "domain/visualization/playbackscenestate.h"
#include "visualplaybackclock.h"
#include "domain/settings/backgroundimagealignment.h"

#include <QVulkanWindow>
#include <QFont>
#include <QImage>

namespace midi_play::presentation::visualization {

// QWidget 宿主通过窗口容器拥有本对象；渲染器由 QVulkanWindow 接管。
class FallingNotesVulkanWindow final : public QVulkanWindow {
    Q_OBJECT
public:
    // 采用单采样 UNORM 色彩输出，并在隐藏期间保留 Qt 管理的持久资源。
    explicit FallingNotesVulkanWindow(QWindow* parent = nullptr);

    // 替换共享只读谱面，并请求一次重绘。
    void setChart(midi_play::visualization::VisualChartPtr chart);
    // 存储限制在 0..durationUs 内的微秒位置，视觉时钟可在绘制时覆盖此值。
    void setTransportPosition(qint64 positionUs, qint64 durationUs);
    // 更新播放状态；成功绘制后仅在播放且窗口可见时连续请求后续帧。
    void setTransportState(midi_play::playback::State state);
    // 简谱带影响布局；效果开关/等级由场景构建器转换为表现参数。
    void setShowNotationStrip(bool show);
    void setVisualEffectsEnabled(bool enabled);
    void setVisualEffectsLevel(midi_play::settings::VisualEffectLevel level);
    // 规范化主题和配色设置，后续 prepare 依据版本更新相关批次。
    void setThemeMode(midi_play::settings::ThemeMode mode);
    void setNoteColorMode(midi_play::settings::NoteColorMode mode);
    // 保存已解码背景并推进版本，每张交换链图像独立追踪上传状态。
    void setBackgroundImage(const QImage& image);
    // 更新背景布局或透明度，无需重新解码或更换原图。
    void setBackgroundImageAlignment(midi_play::settings::BackgroundImageAlignment alignment);
    void setBackgroundImageOpacity(int opacity);
    // 当前输入状态的只读查询；positionUs 返回采样位置，而非视觉插值位置。
    midi_play::visualization::VisualChartPtr chart() const { return m_chart; }
    qint64 positionUs() const noexcept { return m_positionUs; }
    qint64 durationUs() const noexcept { return m_durationUs; }
    const QImage& backgroundImage() const noexcept { return m_backgroundImage; }
    quint64 backgroundRevision() const noexcept { return m_backgroundRevision; }
    midi_play::settings::BackgroundImageAlignment backgroundImageAlignment() const noexcept
    { return m_backgroundAlignment; }
    int backgroundImageOpacity() const noexcept { return m_backgroundOpacity; }
    midi_play::playback::State transportState() const noexcept { return m_state; }
    // 绘制时采样共享视觉时钟，构造独立场景状态及可见时间窗口。
    midi_play::visualization::PlaybackSceneState sceneState() const;
    // 场景文字使用的字体快照；设置字体会请求重绘。
    QFont sceneFont() const { return m_font; }
    void setSceneFont(const QFont& font) { m_font = font; requestUpdate(); }
    // 加载状态清除旧错误，错误状态结束加载，两者均由覆盖层显示。
    void setLoading(bool value) { m_loading = value; if (value) m_error.clear(); requestUpdate(); }
    void setErrorMessage(const QString& message) { m_error = message; m_loading = false; requestUpdate(); }
    // 渲染器完成提交/呈现后的通知入口，驱动成功信号和连续播放帧。
    void frameCompleted(bool success);
    // 借用宿主视觉时钟；宿主必须保证时钟晚于本窗口销毁。
    void setVisualClock(const VisualPlaybackClock* clock) { m_visualClock = clock; }
    // 更新瞬态效果起点，时间单位为微秒；本调用自身不请求重绘。
    void setEffectsStart(qint64 time) { m_effectsStartUs = time; }

    // 工厂返回的渲染器交由 Qt 管理，资源回调使用当前窗口的设备。
    QVulkanWindowRenderer* createRenderer() override;

signals:
    // 初始化、设备丢失或绘制失败时通知宿主，宿主通过事件循环回退。
    void initializationFailed(const QString& message);
    // 每个成功完成的帧均发出；首帧模式确认由宿主去重。
    void frameRendered();

private:
    class Renderer;
    midi_play::visualization::VisualChartPtr m_chart;
    qint64 m_positionUs = 0;
    qint64 m_durationUs = 0;
    midi_play::playback::State m_state = midi_play::playback::State::Empty;
    bool m_loading = false;
    bool m_showNotationStrip = false;
    bool m_visualEffectsEnabled = true;
    midi_play::settings::VisualEffectLevel m_visualEffectsLevel =
        midi_play::settings::kDefaultVisualEffectLevel;
    midi_play::settings::ThemeMode m_themeMode = midi_play::settings::kDefaultThemeMode;
    midi_play::settings::NoteColorMode m_noteColorMode = midi_play::settings::kDefaultNoteColorMode;
    QString m_error;
    QFont m_font;
    const VisualPlaybackClock* m_visualClock = nullptr; // 不拥有时钟；为空时使用采样位置。
    qint64 m_effectsStartUs = 0;
    QImage m_backgroundImage;
    midi_play::settings::BackgroundImageAlignment m_backgroundAlignment =
        midi_play::settings::kDefaultBackgroundImageAlignment;
    int m_backgroundOpacity = midi_play::settings::kDefaultBackgroundImageOpacity;
    quint64 m_backgroundRevision = 1; // 非零初值使空背景也能初始化有效描述符绑定。
};

} // namespace midi_play::presentation::visualization
