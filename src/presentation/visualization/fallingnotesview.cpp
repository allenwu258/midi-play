// 可视化宿主实现：汇总控制器输入，管理重绘、缓存及图形后端生命周期。
#include "fallingnotesview.h"
#include "backgroundimageloader.h"
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
#include <QtConcurrent/QtConcurrentRun>

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
        // Vulkan 窗口自行请求后续帧，宿主计时器只驱动传统绘制。
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
    // 索引重建后旧候选 span 不再有效，必须先清空再等待下一帧重算。
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
    // 两个后端在绘制时采样同一个有界视觉时钟，避免显示位置分歧。
    update();
}

void FallingNotesView::setTransportState(midi_play::playback::State state)
{
    // 进入播放状态时重设瞬态起点，恢复播放不会补播此前的击键动画。
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

void FallingNotesView::setShowNotationStrip(bool show)
{
    if (m_state.showNotationStrip == show) return;
    m_state.showNotationStrip = show;
    m_geometryDirty = true;
#if MIDI_PLAY_HAS_VULKAN
    if (m_vulkanWindow) m_vulkanWindow->setShowNotationStrip(show);
#endif
    update();
}

void FallingNotesView::setVisualEffectsEnabled(bool enabled)
{
    if (m_state.visualEffectsEnabled == enabled) return;
    m_state.visualEffectsEnabled = enabled;
#if MIDI_PLAY_HAS_VULKAN
    if (m_vulkanWindow) m_vulkanWindow->setVisualEffectsEnabled(enabled);
#endif
    update();
}

void FallingNotesView::setVisualEffectsLevel(midi_play::settings::VisualEffectLevel level)
{
    const auto normalized = midi_play::settings::normalizeVisualEffectLevel(level);
    if (m_state.visualEffectsLevel == normalized) return;
    m_state.visualEffectsLevel = normalized;
#if MIDI_PLAY_HAS_VULKAN
    if (m_vulkanWindow) m_vulkanWindow->setVisualEffectsLevel(normalized);
#endif
    update();
}

void FallingNotesView::setThemeMode(midi_play::settings::ThemeMode mode)
{
    const auto normalized = midi_play::settings::normalizeThemeMode(mode);
    if (m_state.themeMode == normalized) return;
    m_state.themeMode = normalized;
    m_staticKeyboardDirty = true;
#if MIDI_PLAY_HAS_VULKAN
    if (m_vulkanWindow) m_vulkanWindow->setThemeMode(normalized);
#endif
    update();
}

void FallingNotesView::setNoteColorMode(midi_play::settings::NoteColorMode mode)
{
    const auto normalized = midi_play::settings::normalizeNoteColorMode(mode);
    if (m_state.noteColorMode == normalized) return;
    m_state.noteColorMode = normalized;
#if MIDI_PLAY_HAS_VULKAN
    if (m_vulkanWindow) m_vulkanWindow->setNoteColorMode(normalized);
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
    m_graphicsModeResolutionPending = false;
    emit graphicsModeResolved(m_graphicsMode);
    return;
#else
    if (m_graphicsMode == normalized && (normalized == midi_play::settings::GraphicsMode::Traditional || m_vulkanWindow)) return;
    m_graphicsMode = normalized;
    if (normalized == midi_play::settings::GraphicsMode::VulkanExperimental) {
        // 创建成功只表示窗口已建立；需等 frameRendered 确认实际首帧成功。
        m_graphicsModeResolutionPending = true;
        if (!createVulkanView()) {
            m_graphicsMode = midi_play::settings::GraphicsMode::Traditional;
            m_graphicsModeResolutionPending = false;
            emit graphicsModeResolved(m_graphicsMode);
            qWarning() << "Vulkan view unavailable; keeping traditional renderer";
        }
    } else {
        m_graphicsModeResolutionPending = false;
        destroyVulkanView();
        emit graphicsModeResolved(m_graphicsMode);
    }
    update();
#endif
}

void FallingNotesView::setBackgroundImagePath(const QString& path)
{
    m_backgroundImagePath = path.trimmed();
    // 清空路径同样递增请求号，使仍在运行的旧解码任务失去写回资格。
    const quint64 requestId = ++m_backgroundRequestId;
    m_backgroundImage = {};
#if MIDI_PLAY_HAS_VULKAN
    if (m_vulkanWindow) m_vulkanWindow->setBackgroundImage({});
#endif
    update();
    if (m_backgroundImagePath.isEmpty()) return;

    auto* watcher = new QFutureWatcher<QImage>(this);
    connect(watcher, &QFutureWatcher<QImage>::finished, this, [this, watcher, requestId] {
        const QImage image = watcher->result();
        watcher->deleteLater();
        // finished 回调在宿主线程执行；过期结果只释放 watcher，不覆盖新背景。
        if (requestId != m_backgroundRequestId) return;
        m_backgroundImage = image;
#if MIDI_PLAY_HAS_VULKAN
        if (m_vulkanWindow) m_vulkanWindow->setBackgroundImage(m_backgroundImage);
#endif
        update();
    });
    watcher->setFuture(QtConcurrent::run(&loadBackgroundImage, m_backgroundImagePath));
}

void FallingNotesView::setBackgroundImageAlignment(midi_play::settings::BackgroundImageAlignment alignment)
{
    const auto normalized = midi_play::settings::normalizeBackgroundImageAlignment(alignment);
    if (m_state.backgroundImageAlignment == normalized) return;
    m_state.backgroundImageAlignment = normalized;
#if MIDI_PLAY_HAS_VULKAN
    if (m_vulkanWindow) m_vulkanWindow->setBackgroundImageAlignment(normalized);
#endif
    update();
}

void FallingNotesView::setBackgroundImageOpacity(int opacity)
{
    const int normalized = std::clamp(opacity, 0, 100);
    if (m_state.backgroundImageOpacity == normalized) return;
    m_state.backgroundImageOpacity = normalized;
#if MIDI_PLAY_HAS_VULKAN
    if (m_vulkanWindow) m_vulkanWindow->setBackgroundImageOpacity(normalized);
#endif
    update();
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
    connect(window, &FallingNotesVulkanWindow::frameRendered, this, [this] {
        if (!m_graphicsModeResolutionPending
            || m_graphicsMode != midi_play::settings::GraphicsMode::VulkanExperimental) {
            return;
        }
        m_graphicsModeResolutionPending = false;
        emit graphicsModeResolved(m_graphicsMode);
    });
    m_vulkanWindow->setVisualClock(&m_visualClock);
    m_vulkanWindow->setEffectsStart(m_state.effectsStartUs);
    // 回退延后到事件循环，避免在 Vulkan 渲染回调内部同步销毁窗口。
    connect(window, &FallingNotesVulkanWindow::initializationFailed, this,
            [this](const QString& message) {
                qWarning() << message;
                QMetaObject::invokeMethod(this, [this] { setGraphicsMode(midi_play::settings::GraphicsMode::Traditional); }, Qt::QueuedConnection);
            }, Qt::QueuedConnection);
    // 新后端从完整状态快照开始，切换模式时保留谱面、时间及背景设置。
    m_vulkanWindow->setSceneFont(font());
    m_vulkanWindow->setShowNotationStrip(m_state.showNotationStrip);
    m_vulkanWindow->setVisualEffectsEnabled(m_state.visualEffectsEnabled);
    m_vulkanWindow->setVisualEffectsLevel(m_state.visualEffectsLevel);
    m_vulkanWindow->setThemeMode(m_state.themeMode);
    m_vulkanWindow->setNoteColorMode(m_state.noteColorMode);
    m_vulkanWindow->setChart(m_state.chart);
    m_vulkanWindow->setTransportPosition(m_state.transportPositionUs, m_state.durationUs);
    m_vulkanWindow->setTransportState(m_state.transportState);
    m_vulkanWindow->setErrorMessage(m_state.errorMessage);
    m_vulkanWindow->setLoading(m_state.loading);
    m_vulkanWindow->setBackgroundImage(m_backgroundImage);
    m_vulkanWindow->setBackgroundImageAlignment(m_state.backgroundImageAlignment);
    m_vulkanWindow->setBackgroundImageOpacity(m_state.backgroundImageOpacity);
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
    // createWindowContainer 接管窗口所有权；实例必须晚于窗口释放。
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
        m_geometry = m_layoutEngine.layout(size(), m_state.chart.get(),
                                           m_state.lookAheadUs, m_state.showNotationStrip);
        m_geometryDirty = false;
        m_staticKeyboardDirty = true;
    }
    // 在绘制时取得显示位置，只有位置变化才使候选窗口重新求值。
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
    // DPR 可能在窗口移到另一块屏幕时改变，即使逻辑尺寸未变也需重建底图。
    if (m_staticKeyboardLogicalRect != keyboardLogicalRect
        || m_staticKeyboardPhysicalSize != physicalSize
        || !qFuzzyCompare(m_staticKeyboardDevicePixelRatio, dpr)) {
        m_staticKeyboardDirty = true;
    }
    if (m_staticKeyboardDirty) rebuildStaticKeyboard(dpr, keyboardLogicalRect);

    QPainter painter(this);
    m_renderer.renderStaticBackgroundLayer(painter, m_geometry, m_state, m_backgroundImage);
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
    // 过大或分配失败时保留空缓存，paintEvent 会直接绘制键盘。
    if (requiredBytes > kMaximumStaticKeyboardBytes) return;

    QImage layer(physicalSize, QImage::Format_ARGB32_Premultiplied);
    if (layer.isNull()) return;
    layer.fill(Qt::transparent);
    layer.setDevicePixelRatio(devicePixelRatio);
    QPainter layerPainter(&layer);
    layerPainter.setFont(font());
    // 底图只覆盖键盘区域，平移后仍可复用完整场景的逻辑坐标。
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
    // span 借用窗口缓存内存，只供当前宿主场景使用；导出时会移除此引用。
    m_state.candidateNoteIndices = std::span<const int>(candidates.constData(), candidates.size());
}

} // namespace midi_play::presentation::visualization
