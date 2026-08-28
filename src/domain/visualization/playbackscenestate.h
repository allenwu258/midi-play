#pragma once

#include "domain/playback/playbacktypes.h"
#include "visualchart.h"

#include <span>

namespace midi_play::visualization {

struct PlaybackSceneState {
    void updateVisibleWindow()
    {
        visibleWindowStartUs = transportPositionUs - afterglowUs;
        visibleWindowEndUs = transportPositionUs + lookAheadUs;
    }

    VisualChartPtr chart;
    playback::State transportState = playback::State::Empty;
    VisualTime transportPositionUs = 0;
    VisualTime durationUs = 0;
    VisualTime lookAheadUs = 5'000'000;
    VisualTime afterglowUs = 160'000;
    VisualTime visibilityGuardUs = 250'000;
    VisualTime visibleWindowStartUs = -160'000;
    VisualTime visibleWindowEndUs = 5'000'000;
    std::span<const int> candidateNoteIndices;
    bool loading = false;
    QString errorMessage;
};

} // namespace midi_play::visualization
