#pragma once

// Vulkan 场景打包接口：将共享可视化状态转换为实例批次和文字 alpha 图集。

#include "domain/visualization/activenotelookup.h"
#include "domain/visualization/playbackscenestate.h"
#include "domain/visualization/visiblenoteindex.h"
#include "domain/visualization/visiblenotewindowcache.h"
#include "presentation/theme/apptheme.h"
#include "noteframestate.h"
#include "playbackoverlaytimeline.h"
#include "textlayoutcache.h"
#include "scenelayoutengine.h"
#include "domain/settings/backgroundimagealignment.h"

#include <QHash>
#include <QImage>
#include <array>

namespace midi_play::presentation::visualization {

// 显式 GPU 实例 ABI：七个连续 vec4，与 CPU 领域对象布局无关。
// 属性顺序、112 字节步长必须与管线输入及两个着色器一致。
struct VulkanQuad {
    std::array<float, 4> rect {}; // 逻辑像素 x、y、宽、高；音符的纵向尺寸在顶点阶段计算。
    std::array<float, 4> times {}; // 相对秒：开始、松键、发声结束；末项为图元类型。
    std::array<float, 4> color {}; // 未预乘 alpha 的 sRGB 填充色。
    std::array<float, 4> border {}; // UI 边框颜色，未预乘 alpha。
    std::array<float, 4> activeBorder {}; // 音符头部及特效边缘色；普通 UI 写为 border 的副本。
    std::array<float, 4> uv {}; // 归一化纹理区域：左、上、宽、高。
    // UI：边框宽度、虚线、纹理类型、椭圆类型；音符：光晕外扩、幽灵标记、特效开关、相位。
    std::array<float, 4> options {};
    bool operator==(const VulkanQuad&) const = default;
};
static_assert(sizeof(VulkanQuad) == 112);

// Vulkan 专用视觉强度，不写入共享播放模型，不影响谱面时间或传统后端。
struct VulkanEffectsProfile {
    float noteHaloStrength = 0.30f;
    float noteEdgeStrength = 0.72f;
    float noteSheenStrength = 0.42f;
    float strikeGlowStrength = 0.28f;
    float keyGlowStrength = 0.34f;
    float particleStrength = 0.72f;
};

// 固定 UI 层序；活动白键必须先于所有黑键绘制，包括未激活的黑键。
enum class VulkanUiLayer : size_t { Background, Strike, WhiteKeys, BlackKeys, Labels, Overlay, Count };

// 某层在实例缓冲中的连续范围，用于 firstInstance/count 绘制。
struct VulkanInstanceRange {
    uint32_t first = 0;
    uint32_t count = 0;
};

// 按层顺序追加实例并记录边界；空层仍需调用 beginLayer，最后调用 finish。
struct VulkanUiBatch {
    QVector<VulkanQuad> quads;
    // 多一个哨兵项；offsets[i+1]-offsets[i] 是第 i 层长度，构建结束后必须单调不减。
    std::array<uint32_t, size_t(VulkanUiLayer::Count) + 1> offsets {};

    // 开始一轮构建；清空实例及所有层偏移。
    void clear() { quads.clear(); offsets.fill(0); }
    // 记录当前层起点，调用顺序必须与 VulkanUiLayer 一致。
    void beginLayer(VulkanUiLayer layer) { offsets[size_t(layer)] = uint32_t(quads.size()); }
    // 写入哨兵边界，使最后一层也能由相邻偏移计算实例数。
    void finish() { offsets.back() = uint32_t(quads.size()); }
    // 返回借助相邻边界计算的层范围，不复制实例。
    VulkanInstanceRange range(VulkanUiLayer layer) const
    {
        const auto i = size_t(layer);
        return {offsets[i], offsets[i + 1] - offsets[i]};
    }
};

// 仅管理 CPU 数据及缓存；GPU 上传与同步由窗口或离屏渲染器负责。
class VulkanScene final {
public:
    // 按失效条件更新布局/实例/图集，并求值当前时间的动态装饰。
    // logicalSize 为布局逻辑像素，dpr 用于文字栅格化；实例仍保存逻辑像素坐标。
    void prepare(const midi_play::visualization::PlaybackSceneState& state,
                 QSize logicalSize, qreal dpr, const QFont& font,
                 bool hasBackground = false, QSize backgroundSize = {}, quint64 backgroundRevision = 0,
                 const SceneLayoutConstraints& constraints = {});
    // 返回内部批次的借用引用；下一次 prepare 可能重建内容。
    const QVector<VulkanQuad>& notes() const { return m_notes; }
    const VulkanUiBatch& staticUi() const { return m_staticUi; }
    const VulkanUiBatch& dynamicUi() const { return m_dynamicUi; }
    // 各 revision 供 GPU 资源副本独立判断是否需要重新上传。
    quint64 staticUiRevision() const { return m_staticUiRevision; }
    const QImage& atlas() const { return m_atlas; }
    quint64 atlasRevision() const { return m_atlasRevision; }
    quint64 notesRevision() const { return m_notesRevision; }
    // 当前分钟的微秒时间基准，音符实例和帧位置均需减去该基准。
    qint64 timeOriginUs() const { return m_timeOriginUs; }
    // 当前几何、可见计数和帧表现参数，供实时/离屏后端共同使用。
    const PlaybackSceneGeometry& geometry() const { return m_geometry; }
    qsizetype visibleNoteCount() const { return m_visibleNoteCount; }
    qreal bodyOpacity() const { return m_noteFrame.bodyOpacity(); }
    const VulkanEffectsProfile& effectsProfile() const { return m_effectsProfile; }

private:
    // 一整段排版文字在图集中的物理像素区域与显示逻辑尺寸。
    struct Glyph { QRect pixels; QSizeF size; };
    // 查询或以货架式排布追加文字；图集装不下时标记下一帧回收。
    Glyph glyph(const QString& text, const QFont& font, qreal maximumWidth);
    // 将文字区域转换为引用图集 alpha 的 UI 实例，并应用对齐/宽度限制。
    void text(QVector<VulkanQuad>& output, const QString& value, const QFont& font,
              const QRectF& rect, const QColor& color, Qt::Alignment alignment,
              qreal maximumWidth = -1);
    // 追加非空矩形/椭圆实例，边框和纹理语义在着色器中解释。
    void rect(QVector<VulkanQuad>& output, const QRectF& rect, const QColor& fill,
              const QColor& border = Qt::transparent, float width = 0, bool ellipse = false);
    // 将候选音符按尾部、主体、音头、震音四层打包，并推进音符版本。
    void rebuildNotes(const midi_play::visualization::PlaybackSceneState& state);
    // 每帧生成网格、击键光效、活动键、文字与状态覆盖层。
    void buildDecorations(const midi_play::visualization::PlaybackSceneState& state, const QFont& font);
    // 重建背景和键盘几何；固定文字留在依赖图集的动态批次中。
    void rebuildStaticUi();

    midi_play::visualization::VisualChartPtr m_chart;
    midi_play::visualization::VisibleNoteIndex m_index;
    midi_play::visualization::VisibleNoteWindowCache m_window;
    midi_play::visualization::ActiveNoteLookup m_active;
    NoteRenderCache m_cache;
    NoteFrameState m_noteFrame;
    PlaybackOverlayTimeline m_overlay;
    PlaybackSceneGeometry m_geometry;
    theme::VisualizationColors m_theme = theme::themeFor(midi_play::settings::kDefaultThemeMode).visualization;
    midi_play::settings::ThemeMode m_themeMode = midi_play::settings::kDefaultThemeMode;
    QSize m_size;
    SceneLayoutConstraints m_layoutConstraints;
    qint64 m_lookAheadUs = 0;
    bool m_showNotationStrip = false;
    bool m_visualEffectsEnabled = true;
    midi_play::settings::VisualEffectLevel m_visualEffectsLevel =
        midi_play::settings::kDefaultVisualEffectLevel;
    VulkanEffectsProfile m_effectsProfile;
    bool m_hasBackground = false;
    QSize m_backgroundSize;
    midi_play::settings::BackgroundImageAlignment m_backgroundAlignment =
        midi_play::settings::kDefaultBackgroundImageAlignment;
    int m_backgroundOpacity = midi_play::settings::kDefaultBackgroundImageOpacity;
    quint64 m_backgroundRevision = 0;
    // 图集按物理像素生成；主题颜色由实例提供，文字覆盖率可跨主题复用。
    qreal m_dpr = 1;
    QImage m_atlas;
    QHash<QString, Glyph> m_glyphs;
    TextLayoutCache m_textLayouts;
    // 当前货架的写入位置与行高，边缘及条目间保留透明间隔。
    int m_atlasX = 1;
    int m_atlasY = 1;
    int m_rowHeight = 0;
    bool m_atlasFull = false;
    quint64 m_atlasRevision = 0;
    quint64 m_notesRevision = 0; // 实例内容变更才递增，纯时间推进由帧常量驱动。
    quint64 m_consumedMaterialRevision = 0; // 已打包到音符实例的共享材质版本。
    quint64 m_staticUiRevision = 0;
    qint64 m_timeOriginUs = 0;
    qsizetype m_visibleNoteCount = 0;
    QVector<VulkanQuad> m_notes;
    VulkanUiBatch m_staticUi;
    VulkanUiBatch m_dynamicUi;
};

} // namespace midi_play::presentation::visualization
