#include "fallingnotesview.h"
#include "domain/settings/playersettings.h"
#if MIDI_PLAY_HAS_VULKAN
#include "fallingnotesvulkanwindow.h"
#endif

#include <QHideEvent>
#include <QEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QResizeEvent>
#if MIDI_PLAY_HAS_VULKAN
#include <QVBoxLayout>
#endif
#include <QDebug>

#include <algorithm>
#include <cmath>

namespace midi_play::presentation::visualization {

FallingNotesView::FallingNotesView(QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("fallingNotesView"));
    setAttribute(Qt::WA_OpaquePaintEvent);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_frameTimer.setTimerType(Qt::PreciseTimer);
    setRefreshRate(60);
    connect(&m_frameTimer, &QChronoTimer::timeout, this, [this] {
        if (!isVisible()) return;
#if MIDI_PLAY_HAS_VULKAN
        if (m_vulkanWindow) return;
#endif
        update();
    });
}

void FallingNotesView::setChart(midi_play::visualization::VisualChartPtr chart)
{
    m_state.chart = std::move(chart);
    m_state.effectsStartUs = 0;
    m_visualClock.sample(0, m_state.chart ? m_state.chart->durationUs() : 0);
    if (m_state.chart) {
        m_noteIndex.rebuild(m_state.chart->notes());
        m_state.durationUs = m_state.chart->durationUs();
        m_state.errorMessage.clear();
    } else {
        m_noteIndex = {};
        m_state.durationUs = 0;
    }
    m_noteWindowCache.reset();
    m_state.candidateNoteIndices = {};
    m_geometryDirty = true;
    m_frameStateDirty = true;
    m_staticKeyboardDirty = true;
#if MIDI_PLAY_HAS_VULKAN
    if (m_vulkanWindow) m_vulkanWindow->setChart(m_state.chart);
#endif
    update();
}

void FallingNotesView::setTransportPosition(qint64 positionUs, qint64 durationUs, qint64 sampledAtUs)
{
    m_state.transportPositionUs = std::clamp<qint64>(positionUs, 0, std::max<qint64>(0, durationUs));
    m_state.durationUs = std::max<qint64>(0, durationUs);
    m_visualClock.sample(m_state.transportPositionUs, m_state.durationUs, sampledAtUs);
    m_frameStateDirty = true;
#if MIDI_PLAY_HAS_VULKAN
    if (m_vulkanWindow) m_vulkanWindow->setTransportPosition(m_state.transportPositionUs, m_state.durationUs);
#endif
    // Both consumers sample the same bounded presentation clock at draw time.
    update();
}

void FallingNotesView::setTransportState(midi_play::playback::State state)
{
    if (state == midi_play::playback::State::Playing && m_state.transportState != state)
        resetTransientEffects(m_state.transportPositionUs);
    m_state.transportState = state;
    m_visualClock.setPlaying(state == midi_play::playback::State::Playing);
    if (state == midi_play::playback::State::Playing) m_frameTimer.start();
    else m_frameTimer.stop();
    m_frameStateDirty = true;
#if MIDI_PLAY_HAS_VULKAN
    if (m_vulkanWindow) m_vulkanWindow->setTransportState(state);
#endif
    update();
}

void FallingNotesView::setPlaybackRate(int percent)
{
    m_visualClock.setRate(percent);
}

void FallingNotesView::setRefreshRate(int hz)
{
    m_frameTimer.setInterval(midi_play::settings::visualizationRefreshPeriod(hz));
}

void FallingNotesView::resetTransientEffects(qint64 positionUs)
{
    m_state.effectsStartUs = std::max<qint64>(0, positionUs);
#if MIDI_PLAY_HAS_VULKAN
    if (m_vulkanWindow) m_vulkanWindow->setEffectsStart(m_state.effectsStartUs);
#endif
}

void FallingNotesView::setLoading(bool loading)
{
    m_state.loading = loading;
    if (loading) m_state.errorMessage.clear();
#if MIDI_PLAY_HAS_VULKAN
    if (m_vulkanWindow) m_vulkanWindow->setLoading(loading);
#endif
    update();
}

void FallingNotesView::setErrorMessage(const QString& message)
{
    m_state.errorMessage = message;
    m_state.loading = false;
#if MIDI_PLAY_HAS_VULKAN
    if (m_vulkanWindow) m_vulkanWindow->setErrorMessage(message);
#endif
    update();
}

FallingNotesView::~FallingNotesView()
{
#if MIDI_PLAY_HAS_VULKAN
    destroyVulkanView();
#endif
}

void FallingNotesView::setGraphicsMode(midi_play::settings::GraphicsMode mode)
{
    const auto normalized = midi_play::settings::normalizeGraphicsMode(mode);
#if !MIDI_PLAY_HAS_VULKAN
    Q_UNUSED(normalized)
    m_graphicsMode = midi_play::settings::GraphicsMode::Traditional;
    return;
#else
    if (m_graphicsMode == normalized && (normalized == midi_play::settings::GraphicsMode::Traditional || m_vulkanWindow)) return;
    m_graphicsMode = normalized;
    if (normalized == midi_play::settings::GraphicsMode::VulkanExperimental) {
        if (!createVulkanView()) {
            m_graphicsMode = midi_play::settings::GraphicsMode::Traditional;
            qWarning() << "Vulkan view unavailable; keeping traditional renderer";
        }
    } else {
        destroyVulkanView();
    }
    update();
#endif
}

#if MIDI_PLAY_HAS_VULKAN
bool FallingNotesView::createVulkanView()
{
    if (m_vulkanWindow) return true;
    m_vulkanInstance = std::make_unique<QVulkanInstance>();
    if (!m_vulkanInstance->create()) {
        m_vulkanInstance.reset();
        return false;
    }
    auto* window = new FallingNotesVulkanWindow();
    window->setVulkanInstance(m_vulkanInstance.get());
    m_vulkanContainer = QWidget::createWindowContainer(window, this);
    if (!m_vulkanContainer) {
        delete window;
        m_vulkanInstance.reset();
        return false;
    }
    m_vulkanContainer->setFocusPolicy(Qt::StrongFocus);
    if (!layout()) {
        auto* root = new QVBoxLayout(this);
        root->setContentsMargins(0, 0, 0, 0);
    }
    layout()->addWidget(m_vulkanContainer);
    m_vulkanWindow = window;
    m_vulkanWindow->setVisualClock(&m_visualClock);
    m_vulkanWindow->setEffectsStart(m_state.effectsStartUs);
    connect(window, &FallingNotesVulkanWindow::initializationFailed, this,
            [this](const QString& message) {
                qWarning() << message;
                QMetaObject::invokeMethod(this, [this] { setGraphicsMode(midi_play::settings::GraphicsMode::Traditional); }, Qt::QueuedConnection);
            }, Qt::QueuedConnection);
    m_vulkanWindow->setSceneFont(font());
    m_vulkanWindow->setChart(m_state.chart);
    m_vulkanWindow->setTransportPosition(m_state.transportPositionUs, m_state.durationUs);
    m_vulkanWindow->setTransportState(m_state.transportState);
    m_vulkanWindow->setErrorMessage(m_state.errorMessage);
    m_vulkanWindow->setLoading(m_state.loading);
    m_vulkanWindow->show();
    return true;
}
#endif

#if MIDI_PLAY_HAS_VULKAN
void FallingNotesView::destroyVulkanView()
{
    if (!m_vulkanContainer) return;
    if (m_vulkanWindow) {
        m_vulkanWindow->setTransportState(midi_play::playback::State::Paused);
        m_vulkanWindow->hide();
    }
    m_vulkanWindow = nullptr;
    delete m_vulkanContainer;
    m_vulkanContainer = nullptr;
    m_vulkanInstance.reset();
}
#endif

void FallingNotesView::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event)
#if MIDI_PLAY_HAS_VULKAN
    if (m_vulkanWindow) return;
#endif
    if (m_geometryDirty) {
        m_geometry = m_layoutEngine.layout(size(), m_state.chart.get(), m_state.lookAheadUs);
        m_geometryDirty = false;
        m_staticKeyboardDirty = true;
    }
    const qint64 displayedPosition = m_visualClock.position();
    if (m_state.transportPositionUs != displayedPosition) {
        m_state.transportPositionUs = displayedPosition;
        m_frameStateDirty = true;
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

    if (!m_state.chart || m_noteIndex.isEmpty()) {
        m_state.candidateNoteIndices = {};
        return;
    }
    m_state.updateVisibleWindow();
    m_noteWindowCache.ensure(m_noteIndex, m_state.visibleWindowStartUs,
                             m_state.visibleWindowEndUs, m_state.visibilityGuardUs);
    const auto& candidates = m_noteWindowCache.candidateNoteIndices();
    m_state.candidateNoteIndices = std::span<const int>(candidates.constData(), candidates.size());
}

} // namespace midi_play::presentation::visualization
