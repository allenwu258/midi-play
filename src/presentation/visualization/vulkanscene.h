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
};
static_assert(sizeof(VulkanQuad) == 112);

class VulkanScene final {
public:
    void prepare(const midi_play::visualization::PlaybackSceneState& state,
                 QSize logicalSize, qreal dpr, const QFont& font);
    const QVector<VulkanQuad>& notes() const { return m_notes; }
    const QVector<VulkanQuad>& background() const { return m_background; }
    const QVector<VulkanQuad>& foreground() const { return m_foreground; }
    const QImage& atlas() const { return m_atlas; }
    quint64 atlasRevision() const { return m_atlasRevision; }
    quint64 notesRevision() const { return m_notesRevision; }
    qint64 timeOriginUs() const { return m_timeOriginUs; }
    const PlaybackSceneGeometry& geometry() const { return m_geometry; }
    qsizetype visibleNoteCount() const { return m_visibleNoteCount; }

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

    midi_play::visualization::VisualChartPtr m_chart;
    midi_play::visualization::VisibleNoteIndex m_index;
    midi_play::visualization::VisibleNoteWindowCache m_window;
    midi_play::visualization::ActiveNoteLookup m_active;
    NoteRenderCache m_cache;
    PlaybackOverlayTimeline m_overlay;
    PlaybackSceneGeometry m_geometry;
    VisualizationTheme m_theme;
    QSize m_size;
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
    qint64 m_timeOriginUs = 0;
    qsizetype m_visibleNoteCount = 0;
    QVector<VulkanQuad> m_notes;
    QVector<VulkanQuad> m_background;
    QVector<VulkanQuad> m_foreground;
};

} // namespace midi_play::presentation::visualization
