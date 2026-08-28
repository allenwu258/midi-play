#include "noterendercache.h"

#include <QColor>
#include <QHash>

#include <algorithm>

namespace midi_play::presentation::visualization {
namespace {

using midi_play::visualization::ColorRgba;
using midi_play::visualization::TremoloNote;
using midi_play::visualization::VisualChart;
using midi_play::visualization::VisualNote;

QColor toColor(const ColorRgba& color)
{
    return QColor(color.red, color.green, color.blue, color.alpha);
}

quint64 styleKey(const VisualNote& note)
{
    const quint64 track = static_cast<quint64>(std::max(0, note.trackIndex));
    const qreal velocity = std::clamp(note.velocity / 127.0, 0.0, 1.0);
    const quint64 lightness = static_cast<quint64>(88.0 + velocity * 30.0);
    const quint64 alpha = note.isGhost()
        ? 120U : static_cast<quint64>(190.0 + velocity * 55.0);
    return (track << 17U) | (lightness << 9U) | (alpha << 1U)
        | static_cast<quint64>(note.isGhost());
}

NoteRenderStyle makeStyle(const VisualChart& chart, const VisualNote& note)
{
    QColor fill = note.trackIndex >= 0 && note.trackIndex < chart.tracks().size()
        ? toColor(chart.tracks().at(note.trackIndex).color)
        : QColor(Qt::white);
    const qreal velocity = std::clamp(note.velocity / 127.0, 0.0, 1.0);
    fill = fill.lighter(static_cast<int>(88.0 + velocity * 30.0));
    fill.setAlpha(note.isGhost() ? 120 : static_cast<int>(190 + velocity * 55));

    QColor tail = fill;
    tail.setAlpha(std::max(38, fill.alpha() / 3));
    QColor inactiveBorder = fill.lighter(112);
    inactiveBorder.setAlpha(240);
    QColor activeBorder = fill.lighter(145);
    activeBorder.setAlpha(240);

    NoteRenderStyle style;
    style.fillBrush = QBrush(fill);
    style.tailBrush = QBrush(tail);
    style.inactiveBorderPen = QPen(inactiveBorder, 1);
    style.activeBorderPen = QPen(activeBorder, 2);
    style.inactiveAttackLinePen = QPen(inactiveBorder, 1);
    style.activeAttackLinePen = QPen(activeBorder, 1);
    if (note.isGhost()) {
        style.inactiveBorderPen.setStyle(Qt::DashLine);
        style.activeBorderPen.setStyle(Qt::DashLine);
    }
    return style;
}

} // namespace

void NoteRenderCache::prepare(const midi_play::visualization::VisualChartPtr& chart,
                              const PlaybackSceneGeometry& geometry)
{
    if (m_chart.get() != chart.get()) {
        rebuildChart(chart);
        m_geometrySize = {};
    }
    if (m_geometrySize != geometry.bounds.size()) {
        rebuildGeometry(geometry);
    }
}

void NoteRenderCache::clear()
{
    m_chart = nullptr;
    m_geometrySize = {};
    m_styles.clear();
    m_notes.clear();
}

const PreparedNoteRenderData* NoteRenderCache::note(int noteIndex) const
{
    return noteIndex >= 0 && noteIndex < m_notes.size() ? &m_notes[noteIndex] : nullptr;
}

const NoteRenderStyle* NoteRenderCache::styleForNote(int noteIndex) const
{
    const auto* prepared = note(noteIndex);
    return prepared && prepared->styleIndex >= 0 && prepared->styleIndex < m_styles.size()
        ? &m_styles[prepared->styleIndex]
        : nullptr;
}

void NoteRenderCache::rebuildChart(const midi_play::visualization::VisualChartPtr& chart)
{
    m_chart = chart;
    m_styles.clear();
    m_notes.clear();
    ++m_chartBuildCount;
    if (!m_chart) return;

    m_notes.resize(m_chart->notes().size());
    QHash<quint64, int> styleIndices;
    styleIndices.reserve(std::min(m_chart->notes().size(), m_chart->tracks().size() * 32));
    for (int noteIndex = 0; noteIndex < m_chart->notes().size(); ++noteIndex) {
        const auto& source = m_chart->notes().at(noteIndex);
        const quint64 key = styleKey(source);
        auto styleIt = styleIndices.constFind(key);
        int styleIndex = -1;
        if (styleIt == styleIndices.cend()) {
            styleIndex = m_styles.size();
            styleIndices.insert(key, styleIndex);
            m_styles.push_back(makeStyle(*m_chart, source));
        } else {
            styleIndex = styleIt.value();
        }

        auto& prepared = m_notes[noteIndex];
        prepared.styleIndex = styleIndex;
        prepared.hasTail = source.audibleEndUs > source.keyEndUs;
        prepared.tremolo = (source.flags & TremoloNote) != 0;
    }
}

void NoteRenderCache::rebuildGeometry(const PlaybackSceneGeometry& geometry)
{
    m_geometrySize = geometry.bounds.size();
    ++m_geometryBuildCount;
    if (!m_chart) return;

    for (int noteIndex = 0; noteIndex < m_chart->notes().size(); ++noteIndex) {
        const auto& source = m_chart->notes().at(noteIndex);
        auto& prepared = m_notes[noteIndex];
        const qreal center = source.isPercussion()
            ? (geometry.drumSlot(source.drumLane) ? geometry.drumSlot(source.drumLane)->centerX : 0.0)
            : (geometry.pitchSlot(source.pitch) ? geometry.pitchSlot(source.pitch)->centerX : 0.0);
        const qreal fullWidth = source.isPercussion()
            ? (geometry.drumSlot(source.drumLane) ? geometry.drumSlot(source.drumLane)->noteWidth : 0.0)
            : (geometry.pitchSlot(source.pitch) ? geometry.pitchSlot(source.pitch)->noteWidth : 0.0);
        if (fullWidth <= 0.0) {
            prepared.validGeometry = false;
            continue;
        }

        if (source.coincidentCount > 1) {
            const qreal partWidth = fullWidth / source.coincidentCount;
            prepared.left = center - fullWidth * 0.5 + source.coincidentIndex * partWidth + 0.5;
            prepared.width = std::max<qreal>(1.0, partWidth - 1.0);
        } else {
            prepared.left = center - fullWidth * 0.5;
            prepared.width = fullWidth;
        }
        prepared.validGeometry = true;
    }
}

} // namespace midi_play::presentation::visualization
