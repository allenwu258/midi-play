#pragma once

// QWidget 可视化宿主：维护展示状态、视觉时钟、栅格缓存及可选 Vulkan 子窗口。

#include "domain/playback/playbacktypes.h"
#include "domain/settings/graphicsmode.h"
#include "domain/settings/playersettings.h"
#include "domain/settings/visualeffectlevel.h"
#include "domain/settings/backgroundimagealignment.h"
#include "domain/visualization/playbackscenestate.h"
#include "domain/visualization/visiblenoteindex.h"
#include "domain/visualization/visiblenotewindowcache.h"
#include "fallingnotesrenderer.h"
#include "scenelayoutengine.h"
#include "visualplaybackclock.h"

#include <QImage>
#include <QSize>
#include <QWidget>
#include <QChronoTimer>
#include <QFutureWatcher>
#include <QTimer>
#if MIDI_PLAY_HAS_VULKAN
#include <QVulkanInstance>
#endif
#include <memory>

namespace midi_play::presentation::visualization {

#if MIDI_PLAY_HAS_VULKAN
class FallingNotesVulkanWindow;
#endif

// 所有设置在 GUI 线程汇入同一场景；两个后端共享时间基准和表现参数。
class FallingNotesView final : public QWidget {
    Q_OBJECT
public:
    // 建立传统后端刷新计时器；析构时先释放 Vulkan 窗口，再释放实例。
    explicit FallingNotesView(QWidget* parent = nullptr);
    ~FallingNotesView() override;

    // 布局尺寸建议与当前展示设置的只读查询。
    QSize minimumSizeHint() const override { return {640, 440}; }
    bool showNotationStrip() const noexcept { return m_state.showNotationStrip; }
    bool visualEffectsEnabled() const noexcept { return m_state.visualEffectsEnabled; }
    midi_play::settings::VisualEffectLevel visualEffectsLevel() const noexcept
    { return m_state.visualEffectsLevel; }
    midi_play::settings::ThemeMode themeMode() const noexcept { return m_state.themeMode; }
    midi_play::settings::NoteColorMode noteColorMode() const noexcept { return m_state.noteColorMode; }
    // 首帧成功确认后才认为 Vulkan 就绪，导出入口可据此选择渲染路径。
    bool vulkanReady() const noexcept {
        return m_graphicsMode == midi_play::settings::GraphicsMode::VulkanExperimental
            && !m_graphicsModeResolutionPending;
    }
    // 导出状态副本时移除借用的候选索引，离屏渲染器会自行构建窗口。
    midi_play::visualization::PlaybackSceneState exportState() const {
        auto result = m_state;
        result.candidateNoteIndices = {};
        return result;
    }
    // QImage 隐式共享的背景快照，可供离屏导出持有。
    QImage exportBackground() const { return m_backgroundImage; }

public slots:
    // 替换谱面并重建索引，同时使几何、候选窗口及静态键盘失效。
    void setChart(midi_play::visualization::VisualChartPtr chart);
    // 接收微秒位置和采样时刻；sampledAtUs 为零时按精确位置显示。
    void setTransportPosition(qint64 positionUs, qint64 durationUs, qint64 sampledAtUs = 0);
    // 更新视觉插值速率，单位为百分比。
    void setPlaybackRate(int percent);
    // 设置传统后端的刷新周期，参数由设置模块规范化。
    void setRefreshRate(int hz);
    // 设置瞬态效果的起始时间，防止 seek 或恢复时重放此前的攻击效果。
    void resetTransientEffects(qint64 positionUs);
    // 同步播放状态，并启动/停止传统后端刷新及视觉时钟外推。
    void setTransportState(midi_play::playback::State state);
    // 切换图形后端；Vulkan 创建或初始化失败时回退传统绘制。
    void setGraphicsMode(midi_play::settings::GraphicsMode mode);
    // 异步解码背景；仅最新请求的结果可以写回当前视图。
    void setBackgroundImagePath(const QString& path);
    // 更新背景图解码上限；当前背景保留到新尺寸的异步解码完成。
    void setBackgroundImageMaximumDimension(int dimension);
    // 更新背景放置模式及 0..100 透明度，不重新解码图片。
    void setBackgroundImageAlignment(midi_play::settings::BackgroundImageAlignment alignment);
    void setBackgroundImageOpacity(int opacity);
    // 简谱带开关影响布局，特效开关/等级只影响帧表现。
    void setShowNotationStrip(bool show);
    void setVisualEffectsEnabled(bool enabled);
    void setVisualEffectsLevel(midi_play::settings::VisualEffectLevel level);
    // 主题同时影响静态键盘与材质；音符配色只影响音符材质。
    void setThemeMode(midi_play::settings::ThemeMode mode);
    void setNoteColorMode(midi_play::settings::NoteColorMode mode);
    // 加载时清除旧错误；设置错误时结束加载状态。
    void setLoading(bool loading);
    void setErrorMessage(const QString& message);

signals:
    // 请求后端首帧成功或回退传统绘制时通知一次，报告实际采用的模式。
    void graphicsModeResolved(midi_play::settings::GraphicsMode mode);

protected:
    // 传统后端绘制入口；Vulkan 子窗口存在时由子窗口负责绘制。
    void paintEvent(QPaintEvent* event) override;
    // 尺寸变化使布局失效；字体/样式变化使静态文字底图失效。
    void resizeEvent(QResizeEvent* event) override;
    void changeEvent(QEvent* event) override;

private:
    // 从可见窗口缓存取得借用的候选索引，仅在帧状态失效时更新。
    void rebuildFrameState();
    // 启动一次带当前解码上限的背景加载；请求号使过期任务无法写回。
    void startBackgroundImageLoad(bool clearCurrent);
    // 按 DPR 生成预乘 alpha 键盘底图，内存超过上限时退回直接绘制。
    void rebuildStaticKeyboard(qreal devicePixelRatio, const QRect& logicalRect);
#if MIDI_PLAY_HAS_VULKAN
    // 建立实例、窗口容器、信号连接，并将当前场景完整同步给子窗口。
    bool createVulkanView();
    // 容器拥有子窗口，删除容器完成窗口资源释放后再销毁实例。
    void destroyVulkanView();
#endif

    midi_play::visualization::PlaybackSceneState m_state;
    midi_play::visualization::VisibleNoteIndex m_noteIndex;
    midi_play::visualization::VisibleNoteWindowCache m_noteWindowCache; // 候选 span 的存储所有者。
    SceneLayoutEngine m_layoutEngine;
    FallingNotesRenderer m_renderer;
    PlaybackSceneGeometry m_geometry;
    VisualPlaybackClock m_visualClock;
    QChronoTimer m_frameTimer;
    // 底图同时记录逻辑区域、物理尺寸与 DPR，跨屏移动时也能检测失效。
    QImage m_staticKeyboard;
    QRect m_staticKeyboardLogicalRect;
    QSize m_staticKeyboardPhysicalSize;
    qreal m_staticKeyboardDevicePixelRatio = 0.0;
    bool m_geometryDirty = true;
    bool m_frameStateDirty = true;
    bool m_staticKeyboardDirty = true;
    midi_play::settings::GraphicsMode m_graphicsMode = midi_play::settings::GraphicsMode::Traditional;
    bool m_graphicsModeResolutionPending = false;
    QString m_backgroundImagePath;
    QImage m_backgroundImage;
    int m_backgroundImageMaximumDimension = midi_play::settings::kDefaultBackgroundImageMaximumDimension;
    quint64 m_backgroundRequestId = 0; // 递增请求号用于丢弃过期异步解码结果。
    QTimer m_backgroundLoadDebounceTimer;
    QFutureWatcher<QImage>* m_backgroundWatcher = nullptr;
    bool m_backgroundLoadQueued = false;
#if MIDI_PLAY_HAS_VULKAN
    std::unique_ptr<QVulkanInstance> m_vulkanInstance;
    FallingNotesVulkanWindow* m_vulkanWindow = nullptr;
    QWidget* m_vulkanContainer = nullptr;
#endif
};

} // namespace midi_play::presentation::visualization
