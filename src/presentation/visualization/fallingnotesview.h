#pragma once

#include "domain/playback/playbacktypes.h"
#include "domain/visualization/playbackscenestate.h"
#include "domain/visualization/visiblenoteindex.h"
#include "domain/visualization/visiblenotewindowcache.h"
#include "fallingnotesrenderer.h"
#include "scenelayoutengine.h"

#include <QImage>
#include <QSize>
#include <QWidget>

namespace midi_play::presentation::visualization {

class FallingNotesView final : public QWidget {
    Q_OBJECT
public:
    explicit FallingNotesView(QWidget* parent = nullptr);

    QSize minimumSizeHint() const override { return {640, 440}; }

public slots:
    void setChart(midi_play::visualization::VisualChartPtr chart);
    void setTransportPosition(qint64 positionUs, qint64 durationUs);
    void setTransportState(midi_play::playback::State state);
    void setLoading(bool loading);
    void setErrorMessage(const QString& message);

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void changeEvent(QEvent* event) override;

private:
    void rebuildFrameState();
    void rebuildStaticKeyboard(qreal devicePixelRatio, const QRect& logicalRect);

    midi_play::visualization::PlaybackSceneState m_state;
    midi_play::visualization::VisibleNoteIndex m_noteIndex;
    midi_play::visualization::VisibleNoteWindowCache m_noteWindowCache;
    SceneLayoutEngine m_layoutEngine;
    FallingNotesRenderer m_renderer;
    PlaybackSceneGeometry m_geometry;
    QImage m_staticKeyboard;
    QRect m_staticKeyboardLogicalRect;
    QSize m_staticKeyboardPhysicalSize;
    qreal m_staticKeyboardDevicePixelRatio = 0.0;
    bool m_geometryDirty = true;
    bool m_frameStateDirty = true;
    bool m_staticKeyboardDirty = true;
};

} // namespace midi_play::presentation::visualization
