#pragma once

#include "domain/playback/playbacktypes.h"
#include "activenotelookup.h"
#include "visualchart.h"

#include <QVector>

namespace midi_play::visualization {

struct PlaybackSceneState {
    void resetActiveNotes(qsizetype drumLaneCount)
    {
        activeNoteIndices.clear();
        activeNoteLookup.reset(drumLaneCount);
    }

    void addActiveNote(int noteIndex, const VisualNote& note)
    {
        activeNoteIndices.push_back(noteIndex);
        activeNoteLookup.add(noteIndex, note);
    }

    VisualChartPtr chart;
    playback::State transportState = playback::State::Empty;
    VisualTime transportPositionUs = 0;
    VisualTime durationUs = 0;
    VisualTime lookAheadUs = 5'000'000;
    VisualTime afterglowUs = 160'000;
    QVector<int> visibleNoteIndices;
    QVector<int> activeNoteIndices;
    ActiveNoteLookup activeNoteLookup;
    bool loading = false;
    QString errorMessage;
};

} // namespace midi_play::visualization
