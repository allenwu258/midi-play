#pragma once

#include "domain/visualization/visualchart.h"
#include "scenegeometry.h"

#include <QBrush>
#include <QPen>
#include <QSizeF>
#include <QVector>

namespace midi_play::presentation::visualization {

struct NoteRenderStyle {
    QBrush fillBrush;
    QBrush tailBrush;
    QPen inactiveBorderPen;
    QPen activeBorderPen;
    QPen inactiveAttackLinePen;
    QPen activeAttackLinePen;
};

struct PreparedNoteRenderData {
    int styleIndex = -1;
    qreal left = 0.0;
    qreal width = 0.0;
    bool hasTail = false;
    bool tremolo = false;
    bool validGeometry = false;
};

// Presentation-side immutable data derived from a VisualChart. Styles are
// deduplicated by track/velocity/ghost state, while note X geometry is rebuilt
// only when the scene layout changes.
class NoteRenderCache final {
public:
    void prepare(const midi_play::visualization::VisualChartPtr& chart,
                 const PlaybackSceneGeometry& geometry);
    void clear();

    const QVector<NoteRenderStyle>& styles() const { return m_styles; }
    const QVector<PreparedNoteRenderData>& notes() const { return m_notes; }
    const PreparedNoteRenderData* note(int noteIndex) const;
    const NoteRenderStyle* styleForNote(int noteIndex) const;

    const midi_play::visualization::VisualChart* chart() const { return m_chart.get(); }
    quint64 chartBuildCount() const { return m_chartBuildCount; }
    quint64 geometryBuildCount() const { return m_geometryBuildCount; }

private:
    void rebuildChart(const midi_play::visualization::VisualChartPtr& chart);
    void rebuildGeometry(const PlaybackSceneGeometry& geometry);

    midi_play::visualization::VisualChartPtr m_chart;
    QSizeF m_geometrySize;
    QVector<NoteRenderStyle> m_styles;
    QVector<PreparedNoteRenderData> m_notes;
    quint64 m_chartBuildCount = 0;
    quint64 m_geometryBuildCount = 0;
};

} // namespace midi_play::presentation::visualization
