#pragma once

#include "domain/visualization/activenotelookup.h"
#include "domain/visualization/playbackscenestate.h"
#include "domain/visualization/visiblenoteindex.h"
#include "domain/visualization/visiblenotewindowcache.h"
#include "fallingnotesrenderer.h"
#include "scenelayoutengine.h"

#include <QHash>
#include <QImage>
#include <array>

namespace midi_play::presentation::visualization {

// Explicit vertex ABI; independent of the shared CPU scene's object layout.
struct VulkanQuad {
    std::array<float, 4> rect {};
    std::array<float, 4> times {}; // start, key end, audible end, primitive kind
    std::array<float, 4> color {};
    std::array<float, 4> border {};
    std::array<float, 4> activeBorder {};
    std::array<float, 4> uv {};
    std::array<float, 4> options {}; // width, dashed, texture, ellipse
    bool operator==(const VulkanQuad&) const = default;
};
static_assert(sizeof(VulkanQuad) == 112);

// White-key illumination must precede black keys, including inactive ones.
enum class VulkanUiLayer : size_t { Background, Strike, WhiteKeys, BlackKeys, Labels, Overlay, Count };

struct VulkanInstanceRange {
    uint32_t first = 0;
    uint32_t count = 0;
};

struct VulkanUiBatch {
    QVector<VulkanQuad> quads;
    std::array<uint32_t, size_t(VulkanUiLayer::Count) + 1> offsets {};

    void clear() { quads.clear(); offsets.fill(0); }
    void beginLayer(VulkanUiLayer layer) { offsets[size_t(layer)] = uint32_t(quads.size()); }
    void finish() { offsets.back() = uint32_t(quads.size()); }
    VulkanInstanceRange range(VulkanUiLayer layer) const
    {
        const auto i = size_t(layer);
        return {offsets[i], offsets[i + 1] - offsets[i]};
    }
};

class VulkanScene final {
public:
    void prepare(const midi_play::visualization::PlaybackSceneState& state,
                 QSize logicalSize, qreal dpr, const QFont& font);
    const QVector<VulkanQuad>& notes() const { return m_notes; }
    const VulkanUiBatch& staticUi() const { return m_staticUi; }
    const VulkanUiBatch& dynamicUi() const { return m_dynamicUi; }
    quint64 staticUiRevision() const { return m_staticUiRevision; }
    const QImage& atlas() const { return m_atlas; }
    quint64 atlasRevision() const { return m_atlasRevision; }
    quint64 notesRevision() const { return m_notesRevision; }
    qint64 timeOriginUs() const { return m_timeOriginUs; }
    const PlaybackSceneGeometry& geometry() const { return m_geometry; }
    qsizetype visibleNoteCount() const { return m_visibleNoteCount; }
    qreal bodyOpacity() const { return m_noteFrame.bodyOpacity(); }

private:
    struct Glyph { QRect pixels; QSizeF size; };
    Glyph glyph(const QString& text, const QFont& font, qreal maximumWidth);
    void text(QVector<VulkanQuad>& output, const QString& value, const QFont& font,
              const QRectF& rect, const QColor& color, Qt::Alignment alignment,
              qreal maximumWidth = -1);
    void rect(QVector<VulkanQuad>& output, const QRectF& rect, const QColor& fill,
              const QColor& border = Qt::transparent, float width = 0, bool ellipse = false);
    void rebuildNotes(const midi_play::visualization::PlaybackSceneState& state);
    void buildDecorations(const midi_play::visualization::PlaybackSceneState& state, const QFont& font);
    void rebuildStaticUi();

    midi_play::visualization::VisualChartPtr m_chart;
    midi_play::visualization::VisibleNoteIndex m_index;
    midi_play::visualization::VisibleNoteWindowCache m_window;
    midi_play::visualization::ActiveNoteLookup m_active;
    NoteRenderCache m_cache;
    NoteFrameState m_noteFrame;
    PlaybackOverlayTimeline m_overlay;
    PlaybackSceneGeometry m_geometry;
    VisualizationTheme m_theme;
    QSize m_size;
    qint64 m_lookAheadUs = 0;
    qreal m_dpr = 1;
    QImage m_atlas;
    QHash<QString, Glyph> m_glyphs;
    TextLayoutCache m_textLayouts;
    int m_atlasX = 1;
    int m_atlasY = 1;
    int m_rowHeight = 0;
    bool m_atlasFull = false;
    quint64 m_atlasRevision = 0;
    quint64 m_notesRevision = 0;
    quint64 m_staticUiRevision = 0;
    qint64 m_timeOriginUs = 0;
    qsizetype m_visibleNoteCount = 0;
    QVector<VulkanQuad> m_notes;
    VulkanUiBatch m_staticUi;
    VulkanUiBatch m_dynamicUi;
};

} // namespace midi_play::presentation::visualization
