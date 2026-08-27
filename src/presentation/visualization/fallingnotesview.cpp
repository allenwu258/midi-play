#include "fallingnotesview.h"

#include <QHideEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QResizeEvent>
#include <QShowEvent>

#include <algorithm>

namespace midi_play::presentation::visualization {

FallingNotesView::FallingNotesView(QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("fallingNotesView"));
    setAttribute(Qt::WA_OpaquePaintEvent);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_repaintTimer.setInterval(16);
    m_repaintTimer.setTimerType(Qt::PreciseTimer);
    connect(&m_repaintTimer, &QTimer::timeout, this, QOverload<>::of(&FallingNotesView::update));
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
    m_state.activeNoteIndices.clear();
    m_geometryDirty = true;
    rebuildFrameState();
    update();
}

void FallingNotesView::setTransportPosition(qint64 positionUs, qint64 durationUs)
{
    m_state.transportPositionUs = std::clamp<qint64>(positionUs, 0, std::max<qint64>(0, durationUs));
    m_state.durationUs = std::max<qint64>(0, durationUs);
    rebuildFrameState();
    if (m_state.transportState != midi_play::playback::State::Playing) update();
}

void FallingNotesView::setTransportState(midi_play::playback::State state)
{
    m_state.transportState = state;
    rebuildFrameState();
    updateAnimationTimer();
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
    }
    rebuildFrameState();
    QPainter painter(this);
    m_renderer.render(painter, m_geometry, m_state);
}

void FallingNotesView::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    m_geometryDirty = true;
}

void FallingNotesView::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    updateAnimationTimer();
}

void FallingNotesView::hideEvent(QHideEvent* event)
{
    QWidget::hideEvent(event);
    m_repaintTimer.stop();
}

void FallingNotesView::rebuildFrameState()
{
    if (!m_state.chart || m_noteIndex.isEmpty()) {
        m_state.visibleNoteIndices.clear();
        m_state.activeNoteIndices.clear();
        return;
    }
    m_noteIndex.query(m_state.transportPositionUs - m_state.afterglowUs,
                      m_state.transportPositionUs + m_state.lookAheadUs,
                      m_state.visibleNoteIndices);
    m_state.activeNoteIndices.clear();
    if (m_state.transportState != midi_play::playback::State::Playing) return;
    m_state.activeNoteIndices.reserve(std::min<qsizetype>(64, m_state.visibleNoteIndices.size()));
    for (const int index : m_state.visibleNoteIndices) {
        const auto& note = m_state.chart->notes().at(index);
        if (note.startUs <= m_state.transportPositionUs && note.audibleEndUs > m_state.transportPositionUs) {
            m_state.activeNoteIndices.push_back(index);
        }
    }
}

void FallingNotesView::updateAnimationTimer()
{
    const bool shouldRun = isVisible() && m_state.transportState == midi_play::playback::State::Playing;
    if (shouldRun && !m_repaintTimer.isActive()) m_repaintTimer.start();
    else if (!shouldRun) m_repaintTimer.stop();
}

} // namespace midi_play::presentation::visualization
