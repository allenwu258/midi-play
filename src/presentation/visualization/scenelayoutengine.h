#pragma once

#include "domain/visualization/visualchart.h"
#include "scenegeometry.h"

#include <QSizeF>

namespace midi_play::presentation::visualization {

class SceneLayoutEngine final {
public:
    PlaybackSceneGeometry layout(const QSizeF& viewport,
                                 const midi_play::visualization::VisualChart* chart,
                                 qint64 lookAheadUs) const;

private:
    static bool isBlackKey(int pitch);
};

} // namespace midi_play::presentation::visualization
