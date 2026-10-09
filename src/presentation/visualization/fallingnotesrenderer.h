#pragma once

// 传统绘制后端：将共享场景、材质和帧状态转换为 QPainter 绘制命令。

#include "domain/visualization/activenotelookup.h"
#include "domain/visualization/playbackscenestate.h"
#include "presentation/theme/apptheme.h"
#include "noterendercache.h"
#include "noteframestate.h"
#include "noterastercache.h"
#include "playbackoverlaytimeline.h"
#include "scenegeometry.h"
#include "textlayoutcache.h"

#include <QColor>
#include <QImage>
#include <QLineF>
#include <QRectF>
#include <QVector>

class QPainter;

namespace midi_play::presentation::visualization {

// 持有可复用的布局/纹理缓存；调用者负责提供当前几何及候选音符窗口。
class FallingNotesRenderer final {
public:
    // 依次绘制静态层与动态层，适用于无需单独缓存静态键盘的调用方。
    void render(QPainter& painter, const PlaybackSceneGeometry& geometry,
                const midi_play::visualization::PlaybackSceneState& state);
    // 绘制默认背景和键盘底图，不包含随播放时间变化的元素。
    void renderStaticLayer(QPainter& painter, const PlaybackSceneGeometry& geometry,
                           const midi_play::visualization::PlaybackSceneState& state);
    // 在落音区域内放置可选背景图片，并叠加黑键对应的音高带。
    void renderStaticBackgroundLayer(QPainter& painter, const PlaybackSceneGeometry& geometry,
                                     const midi_play::visualization::PlaybackSceneState& state,
                                     const QImage& background = {});
    // 绘制键盘底色、边框和固定标签，供视图缓存为独立图像。
    void renderStaticKeyboardLayer(QPainter& painter, const PlaybackSceneGeometry& geometry,
                                   const midi_play::visualization::PlaybackSceneState& state);
    // 求值本帧状态，按网格、音符、击键线、键盘高亮、覆盖层顺序绘制。
    void renderDynamicLayer(QPainter& painter, const PlaybackSceneGeometry& geometry,
                            const midi_play::visualization::PlaybackSceneState& state);
    // 以下计数来自最近一次动态层求值，供性能统计读取。
    qsizetype visibleNoteCount() const { return m_visibleNoteCount; }
    qsizetype activeNoteCount() const { return m_activeNoteCount; }

private:
    // 更新主题、谱面材质和键盘几何缓存；不推进播放时间。
    void prepareScene(const PlaybackSceneGeometry& geometry,
                      const midi_play::visualization::PlaybackSceneState& state);
    // 将预计算的黑键音高带批量绘入落音区域。
    void drawPitchBands(QPainter& painter, const PlaybackSceneGeometry& geometry) const;
    // 二分定位可见节拍线，并使用文字布局缓存绘制小节号。
    void drawTimeGrid(QPainter& painter, const PlaybackSceneGeometry& geometry,
                      const midi_play::visualization::PlaybackSceneState& state);
    // 分层绘制尾部、主体、音头与震音标记，各层保持候选音符顺序。
    void drawNotes(QPainter& painter, const PlaybackSceneGeometry& geometry,
                   const midi_play::visualization::PlaybackSceneState& state);
    // 绘制击键光效及可选简谱标签；光效取自共享帧状态。
    void drawStrikeLine(QPainter& painter, const PlaybackSceneGeometry& geometry,
                        const midi_play::visualization::PlaybackSceneState& state);
    // 按白键、黑键、标签、打击乐键顺序生成静态键盘。
    void drawKeyboardBase(QPainter& painter, const PlaybackSceneGeometry& geometry,
                          const midi_play::visualization::PlaybackSceneState& state);
    // 覆盖活动键颜色，恢复黑键遮挡关系，并补绘被覆盖的标签。
    void drawActiveKeyboard(QPainter& painter, const PlaybackSceneGeometry& geometry,
                            const midi_play::visualization::PlaybackSceneState& state);
    // 绘制当前歌词/标记，以及加载、错误或未载入谱面的状态覆盖层。
    void drawOverlay(QPainter& painter, const PlaybackSceneGeometry& geometry,
                     const midi_play::visualization::PlaybackSceneState& state);

    theme::VisualizationColors m_theme = theme::themeFor(midi_play::settings::kDefaultThemeMode).visualization;
    midi_play::visualization::ActiveNoteLookup m_activeNoteLookup;
    PlaybackOverlayTimeline m_overlayTimeline;
    TextLayoutCache m_textLayoutCache;
    NoteRenderCache m_noteRenderCache;
    NoteFrameState m_noteFrame;
    NoteRasterCache m_noteRasterCache;
    // 仅随布局变化重建；黑键前景用于修复白键高亮带来的覆盖。
    QVector<QRectF> m_pitchBandRects;
    QVector<QRectF> m_blackKeyForegroundRects;
    quint64 m_keyboardGeometryBuildCount = 0; // 与音符几何缓存的构建版本同步。
    qsizetype m_visibleNoteCount = 0;
    qsizetype m_activeNoteCount = 0;
};

} // namespace midi_play::presentation::visualization
