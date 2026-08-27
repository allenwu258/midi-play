#pragma once

#include "domain/playback/playbacktypes.h"
#include "visualchart.h"

#include <QVector>

namespace midi_play::visualization {

struct PlaybackSceneState {
    VisualChartPtr chart;
    playback::State transportState = playback::State::Empty;
    VisualTime transportPositionUs = 0;
    VisualTime durationUs = 0;
    VisualTime lookAheadUs = 5'000'000;
    VisualTime afterglowUs = 160'000;
    QVector<int> visibleNoteIndices;
    QVector<int> activeNoteIndices;
    bool loading = false;
    QString errorMessage;
};

} // namespace midi_play::visualization
