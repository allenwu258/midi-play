#include "noterendercache.h"

#include <QColor>
#include <QHash>
#include <QLinearGradient>

#include <algorithm>

namespace midi_play::presentation::visualization {
namespace {

using midi_play::visualization::TremoloNote;
using midi_play::visualization::VisualChart;
using midi_play::visualization::VisualNote;

quint64 styleKey(const VisualNote& note)
{
    return noteMaterialKey(note);
}

NoteRenderStyle makeStyle(const VisualChart& chart, const VisualNote& note,
                          const theme::NoteMaterialProfile& profile)
{
    NoteRenderStyle style;
    style.material = makeNoteMaterial(chart, note, profile);
    const QColor fill = style.material.body;
    const QColor tail = style.material.tail;
    QLinearGradient bodyGradient(0, 0, 1, 0);
    bodyGradient.setCoordinateMode(QGradient::ObjectBoundingMode);
    QColor side = fill;
    side.setAlphaF(fill.alphaF() * 0.66);
    bodyGradient.setColorAt(0, side);
    bodyGradient.setColorAt(0.32, fill);
    side.setAlphaF(fill.alphaF() * 0.78);
    bodyGradient.setColorAt(1, side);
    style.bodyGradientBrush = QBrush(bodyGradient);
    QLinearGradient tailGradient(0, 0, 0, 1);
    tailGradient.setCoordinateMode(QGradient::ObjectBoundingMode);
    QColor tip = tail;
    tip.setAlphaF(tail.alphaF() * 0.16);
    tailGradient.setColorAt(0, tip);
    tailGradient.setColorAt(1, tail);
    style.tailGradientBrush = QBrush(tailGradient);
    return style;
}

} // namespace

void NoteRenderCache::prepare(const midi_play::visualization::VisualChartPtr& chart,
                              const PlaybackSceneGeometry& geometry, midi_play::settings::ThemeMode mode)
{
    const auto normalized = midi_play::settings::normalizeThemeMode(mode);
    const bool themeChanged = m_themeMode != normalized;
    m_themeMode = normalized;
    if (m_chart.get() != chart.get()) {
        rebuildChart(chart);
        m_geometrySize = {};
    } else if (themeChanged) {
        rebuildMaterials();
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
    m_styleRepresentatives.clear();
    m_notes.clear();
    ++m_materialRevision;
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
    m_styleRepresentatives.clear();
    m_notes.clear();
    ++m_chartBuildCount;
    ++m_materialRevision;
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
            m_styles.push_back(makeStyle(*m_chart, source, theme::themeFor(m_themeMode).notes));
            m_styleRepresentatives.push_back(noteIndex);
        } else {
            styleIndex = styleIt.value();
        }

        auto& prepared = m_notes[noteIndex];
        prepared.styleIndex = styleIndex;
        prepared.hasTail = source.audibleEndUs > source.keyEndUs;
        prepared.tremolo = (source.flags & TremoloNote) != 0;
        prepared.startUs = source.startUs;
        prepared.keyEndUs = source.keyEndUs;
        prepared.audibleEndUs = source.audibleEndUs;
        prepared.instanceId = source.instanceId;
        prepared.flags = source.flags;
    }
}

void NoteRenderCache::rebuildMaterials()
{
    ++m_materialRevision;
    if (!m_chart) return;
    const auto& profile = theme::themeFor(m_themeMode).notes;
    for (qsizetype i = 0; i < m_styles.size(); ++i)
        m_styles[i] = makeStyle(*m_chart, m_chart->notes()[m_styleRepresentatives[i]], profile);
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
            const qreal gutter = std::min<qreal>(1.0, partWidth * 0.24);
            prepared.left = center - fullWidth * 0.5 + source.coincidentIndex * partWidth + gutter * 0.5;
            prepared.width = partWidth - gutter;
        } else {
            prepared.left = center - fullWidth * 0.5;
            prepared.width = fullWidth;
        }
        prepared.validGeometry = true;
    }
}

} // namespace midi_play::presentation::visualization
