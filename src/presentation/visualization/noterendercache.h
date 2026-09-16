#pragma once

#include "domain/visualization/visualchart.h"
#include "scenegeometry.h"
#include "notematerial.h"

#include <QBrush>
#include <QSizeF>
#include <QVector>

namespace midi_play::presentation::visualization {

struct NoteRenderStyle {
    NoteMaterial material;
    QBrush bodyGradientBrush;
    QBrush tailGradientBrush;
};

struct PreparedNoteRenderData {
    int styleIndex = -1;
    qreal left = 0.0;
    qreal width = 0.0;
    bool hasTail = false;
    bool tremolo = false;
    bool validGeometry = false;
    // Backend-neutral temporal payload consumed by both raster and GPU paths.
    qint64 startUs = 0;
    qint64 keyEndUs = 0;
    qint64 audibleEndUs = 0;
    quint64 instanceId = 0;
    quint32 flags = 0;
};

// Presentation data derived from a VisualChart. Styles are deduplicated by
// identity/pitch/voice/velocity/ghost/percussion state and recolored independently of note
// timing and X geometry. Geometry is rebuilt only when the scene layout changes.
class NoteRenderCache final {
public:
    void prepare(const midi_play::visualization::VisualChartPtr& chart,
                 const PlaybackSceneGeometry& geometry,
                 midi_play::settings::ThemeMode mode = midi_play::settings::kDefaultThemeMode,
                 midi_play::settings::NoteColorMode colors = midi_play::settings::kDefaultNoteColorMode);
    void clear();

    const QVector<NoteRenderStyle>& styles() const { return m_styles; }
    const QVector<PreparedNoteRenderData>& notes() const { return m_notes; }
    const PreparedNoteRenderData* note(int noteIndex) const;
    const NoteRenderStyle* styleForNote(int noteIndex) const;

    const midi_play::visualization::VisualChart* chart() const { return m_chart.get(); }
    quint64 chartBuildCount() const { return m_chartBuildCount; }
    quint64 geometryBuildCount() const { return m_geometryBuildCount; }
    quint64 materialRevision() const { return m_materialRevision; }
    const NoteAppearance& appearance() const { return *m_appearance; }

private:
    void rebuildChart(const midi_play::visualization::VisualChartPtr& chart);
    void rebuildGeometry(const PlaybackSceneGeometry& geometry);
    void rebuildMaterials();

    midi_play::visualization::VisualChartPtr m_chart;
    QSizeF m_geometrySize;
    QVector<NoteRenderStyle> m_styles;
    QVector<int> m_styleRepresentatives;
    QVector<PreparedNoteRenderData> m_notes;
    quint64 m_chartBuildCount = 0;
    quint64 m_geometryBuildCount = 0;
    quint64 m_materialRevision = 0;
    const NoteAppearance* m_appearance = &noteAppearanceFor();
};

} // namespace midi_play::presentation::visualization
