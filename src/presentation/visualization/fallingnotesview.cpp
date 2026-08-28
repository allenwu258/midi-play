#include "fallingnotesview.h"

#include <QHideEvent>
#include <QEvent>
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
    m_staticKeyboardDirty = true;
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
        m_staticKeyboardDirty = true;
    }
    rebuildFrameState();

    const qreal dpr = std::max<qreal>(1.0, devicePixelRatioF());
    const QRect keyboardLogicalRect = m_geometry.keyboardRect.toAlignedRect();
    const QSize physicalSize(
        std::max(1, static_cast<int>(std::ceil(keyboardLogicalRect.width() * dpr))),
        std::max(1, static_cast<int>(std::ceil(keyboardLogicalRect.height() * dpr))));
    if (m_staticKeyboardLogicalRect != keyboardLogicalRect
        || m_staticKeyboardPhysicalSize != physicalSize
        || !qFuzzyCompare(m_staticKeyboardDevicePixelRatio, dpr)) {
        m_staticKeyboardDirty = true;
    }
    if (m_staticKeyboardDirty) rebuildStaticKeyboard(dpr, keyboardLogicalRect);

    QPainter painter(this);
    m_renderer.renderStaticBackgroundLayer(painter, m_geometry, m_state);
    if (!m_staticKeyboard.isNull()) {
        painter.drawImage(m_staticKeyboardLogicalRect.topLeft(), m_staticKeyboard);
    } else {
        m_renderer.renderStaticKeyboardLayer(painter, m_geometry, m_state);
    }
    m_renderer.renderDynamicLayer(painter, m_geometry, m_state);
}

void FallingNotesView::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    m_geometryDirty = true;
}

void FallingNotesView::changeEvent(QEvent* event)
{
    QWidget::changeEvent(event);
    if (event->type() == QEvent::FontChange
        || event->type() == QEvent::ApplicationFontChange
        || event->type() == QEvent::StyleChange) {
        m_staticKeyboardDirty = true;
        update();
    }
}

void FallingNotesView::rebuildStaticKeyboard(qreal devicePixelRatio,
                                             const QRect& logicalRect)
{
    constexpr qsizetype kMaximumStaticKeyboardBytes = 16 * 1024 * 1024;
    const QSize physicalSize(
        std::max(1, static_cast<int>(std::ceil(logicalRect.width() * devicePixelRatio))),
        std::max(1, static_cast<int>(std::ceil(logicalRect.height() * devicePixelRatio))));
    const qsizetype requiredBytes = static_cast<qsizetype>(physicalSize.width())
                                  * static_cast<qsizetype>(physicalSize.height()) * 4;

    m_staticKeyboard = {};
    m_staticKeyboardLogicalRect = logicalRect;
    m_staticKeyboardPhysicalSize = physicalSize;
    m_staticKeyboardDevicePixelRatio = devicePixelRatio;
    m_staticKeyboardDirty = false;
    if (requiredBytes > kMaximumStaticKeyboardBytes) return;

    QImage layer(physicalSize, QImage::Format_ARGB32_Premultiplied);
    if (layer.isNull()) return;
    layer.fill(Qt::transparent);
    layer.setDevicePixelRatio(devicePixelRatio);
    QPainter layerPainter(&layer);
    layerPainter.setFont(font());
    layerPainter.translate(-logicalRect.topLeft());
    m_renderer.renderStaticKeyboardLayer(layerPainter, m_geometry, m_state);
    layerPainter.end();
    m_staticKeyboard = std::move(layer);
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
