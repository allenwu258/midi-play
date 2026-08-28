#include "fallingnotesview.h"

#include <QHideEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QResizeEvent>

#include <algorithm>
#include <cmath>

namespace midi_play::presentation::visualization {

FallingNotesView::FallingNotesView(QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("fallingNotesView"));
    setAttribute(Qt::WA_OpaquePaintEvent);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

void FallingNotesView::setChart(midi_play::visualization::VisualChartPtr chart)
{
    m_state.chart = std::move(chart);
    if (m_state.chart) {
        m_noteIndex.rebuild(m_state.chart->notes());
        m_state.durationUs = m_state.chart->durationUs();
        m_state.errorMessage.clear();
    } else {
        m_noteIndex = {};
        m_state.durationUs = 0;
    }
    m_state.visibleNoteIndices.clear();
    m_state.resetActiveNotes(m_state.chart ? m_state.chart->drumLanes().size() : 0);
    m_geometryDirty = true;
    m_frameStateDirty = true;
    m_staticLayerDirty = true;
    update();
}

void FallingNotesView::setTransportPosition(qint64 positionUs, qint64 durationUs)
{
    m_state.transportPositionUs = std::clamp<qint64>(positionUs, 0, std::max<qint64>(0, durationUs));
    m_state.durationUs = std::max<qint64>(0, durationUs);
    m_frameStateDirty = true;
    // PlaybackController is the single UI frame clock. Every published
    // transport sample invalidates this view; the Qt event loop coalesces
    // multiple update requests into one paint event when the UI is busy.
    update();
}

void FallingNotesView::setTransportState(midi_play::playback::State state)
{
    m_state.transportState = state;
    m_frameStateDirty = true;
    update();
}

void FallingNotesView::setLoading(bool loading)
{
    m_state.loading = loading;
    if (loading) m_state.errorMessage.clear();
    update();
}

void FallingNotesView::setErrorMessage(const QString& message)
{
    m_state.errorMessage = message;
    m_state.loading = false;
    update();
}

void FallingNotesView::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event)
    if (m_geometryDirty) {
        m_geometry = m_layoutEngine.layout(size(), m_state.chart.get(), m_state.lookAheadUs);
        m_geometryDirty = false;
        m_staticLayerDirty = true;
    }
    rebuildFrameState();

    const qreal dpr = std::max<qreal>(1.0, devicePixelRatioF());
    const QSize physicalSize(
        std::max(1, static_cast<int>(std::ceil(width() * dpr))),
        std::max(1, static_cast<int>(std::ceil(height() * dpr))));
    if (m_staticLayerPhysicalSize != physicalSize
        || !qFuzzyCompare(m_staticLayerDevicePixelRatio, dpr)) {
        m_staticLayerDirty = true;
    }
    if (m_staticLayerDirty) rebuildStaticLayer(dpr);

    QPainter painter(this);
    if (!m_staticLayer.isNull()) {
        painter.drawImage(QPointF(0.0, 0.0), m_staticLayer);
    } else {
        m_renderer.renderStaticLayer(painter, m_geometry, m_state);
    }
    m_renderer.renderDynamicLayer(painter, m_geometry, m_state);
}

void FallingNotesView::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    m_geometryDirty = true;
}

void FallingNotesView::rebuildStaticLayer(qreal devicePixelRatio)
{
    constexpr qsizetype kMaximumStaticLayerBytes = 64 * 1024 * 1024;
    const QSize physicalSize(
        std::max(1, static_cast<int>(std::ceil(width() * devicePixelRatio))),
        std::max(1, static_cast<int>(std::ceil(height() * devicePixelRatio))));
    const qsizetype requiredBytes = static_cast<qsizetype>(physicalSize.width())
                                  * static_cast<qsizetype>(physicalSize.height()) * 4;

    m_staticLayer = {};
    m_staticLayerPhysicalSize = physicalSize;
    m_staticLayerDevicePixelRatio = devicePixelRatio;
    m_staticLayerDirty = false;
    if (requiredBytes > kMaximumStaticLayerBytes) return;

    QImage layer(physicalSize, QImage::Format_RGB32);
    if (layer.isNull()) return;
    layer.setDevicePixelRatio(devicePixelRatio);
    QPainter layerPainter(&layer);
    m_renderer.renderStaticLayer(layerPainter, m_geometry, m_state);
    layerPainter.end();
    m_staticLayer = std::move(layer);
}

void FallingNotesView::rebuildFrameState()
{
    if (!m_frameStateDirty) {
        return;
    }
    m_frameStateDirty = false;

    const qsizetype drumLaneCount = m_state.chart ? m_state.chart->drumLanes().size() : 0;
    m_state.resetActiveNotes(drumLaneCount);
    if (!m_state.chart || m_noteIndex.isEmpty()) {
        m_state.visibleNoteIndices.clear();
        return;
    }
    m_noteIndex.query(m_state.transportPositionUs - m_state.afterglowUs,
                      m_state.transportPositionUs + m_state.lookAheadUs,
                      m_state.visibleNoteIndices);
    if (m_state.transportState != midi_play::playback::State::Playing) return;
    m_state.activeNoteIndices.reserve(std::min<qsizetype>(64, m_state.visibleNoteIndices.size()));
    for (const int index : m_state.visibleNoteIndices) {
        const auto& note = m_state.chart->notes().at(index);
        if (note.startUs <= m_state.transportPositionUs && note.audibleEndUs > m_state.transportPositionUs) {
            m_state.addActiveNote(index, note);
        }
    }
}

} // namespace midi_play::presentation::visualization
