#pragma once

#include "domain/playback/playbacktypes.h"
#include "domain/settings/graphicsmode.h"
#include "domain/visualization/playbackscenestate.h"
#include "domain/visualization/visiblenoteindex.h"
#include "domain/visualization/visiblenotewindowcache.h"
#include "fallingnotesrenderer.h"
#include "scenelayoutengine.h"
#include "visualplaybackclock.h"

#include <QImage>
#include <QSize>
#include <QWidget>
#include <QChronoTimer>
#if MIDI_PLAY_HAS_VULKAN
#include <QVulkanInstance>
#endif
#include <memory>

namespace midi_play::presentation::visualization {

#if MIDI_PLAY_HAS_VULKAN
class FallingNotesVulkanWindow;
#endif

class FallingNotesView final : public QWidget {
    Q_OBJECT
public:
    explicit FallingNotesView(QWidget* parent = nullptr);
    ~FallingNotesView() override;

    QSize minimumSizeHint() const override { return {640, 440}; }
    bool showNotationStrip() const noexcept { return m_state.showNotationStrip; }

public slots:
    void setChart(midi_play::visualization::VisualChartPtr chart);
    void setTransportPosition(qint64 positionUs, qint64 durationUs, qint64 sampledAtUs = 0);
    void setPlaybackRate(int percent);
    void setRefreshRate(int hz);
    void resetTransientEffects(qint64 positionUs);
    void setTransportState(midi_play::playback::State state);
    void setGraphicsMode(midi_play::settings::GraphicsMode mode);
    void setShowNotationStrip(bool show);
    void setLoading(bool loading);
    void setErrorMessage(const QString& message);

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void changeEvent(QEvent* event) override;

private:
    void rebuildFrameState();
    void rebuildStaticKeyboard(qreal devicePixelRatio, const QRect& logicalRect);
#if MIDI_PLAY_HAS_VULKAN
    bool createVulkanView();
    void destroyVulkanView();
#endif

    midi_play::visualization::PlaybackSceneState m_state;
    midi_play::visualization::VisibleNoteIndex m_noteIndex;
    midi_play::visualization::VisibleNoteWindowCache m_noteWindowCache;
    SceneLayoutEngine m_layoutEngine;
    FallingNotesRenderer m_renderer;
    PlaybackSceneGeometry m_geometry;
    VisualPlaybackClock m_visualClock;
    QChronoTimer m_frameTimer;
    QImage m_staticKeyboard;
    QRect m_staticKeyboardLogicalRect;
    QSize m_staticKeyboardPhysicalSize;
    qreal m_staticKeyboardDevicePixelRatio = 0.0;
    bool m_geometryDirty = true;
    bool m_frameStateDirty = true;
    bool m_staticKeyboardDirty = true;
    midi_play::settings::GraphicsMode m_graphicsMode = midi_play::settings::GraphicsMode::Traditional;
#if MIDI_PLAY_HAS_VULKAN
    std::unique_ptr<QVulkanInstance> m_vulkanInstance;
    FallingNotesVulkanWindow* m_vulkanWindow = nullptr;
    QWidget* m_vulkanContainer = nullptr;
#endif
};

} // namespace midi_play::presentation::visualization
