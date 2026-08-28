#pragma once

#include "domain/playback/playbacktypes.h"
#include "domain/visualization/playbackscenestate.h"
#include "domain/visualization/visiblenoteindex.h"
#include "fallingnotesrenderer.h"
#include "scenelayoutengine.h"

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

private:
    void rebuildFrameState();

    midi_play::visualization::PlaybackSceneState m_state;
    midi_play::visualization::VisibleNoteIndex m_noteIndex;
    SceneLayoutEngine m_layoutEngine;
    FallingNotesRenderer m_renderer;
    PlaybackSceneGeometry m_geometry;
    bool m_geometryDirty = true;
    bool m_frameStateDirty = true;
};

} // namespace midi_play::presentation::visualization
