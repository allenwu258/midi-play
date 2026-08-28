#pragma once

#include "visualchart.h"

#include <QVector>

#include <array>
#include <bitset>
#include <cstddef>

namespace midi_play::visualization {

// Per-frame lookup derived from the active-note set. It stores note indices
// rather than presentation colors so visualization state remains independent
// of the rendering backend. Later notes replace earlier notes for keyboard
// highlighting, matching the previous reverse-scan behavior.
class ActiveNoteLookup final {
public:
    static constexpr int MidiPitchCount = 128;

    ActiveNoteLookup();

    void reset(qsizetype drumLaneCount = 0);
    void add(int noteIndex, const VisualNote& note);

    int noteIndexForPitch(int pitch) const;
    int noteIndexForDrumLane(int lane) const;
    const QVector<int>& activePitches() const { return m_activePitches; }
    const QVector<int>& activeDrumLanes() const { return m_activeDrumLanes; }
    const QVector<int>& melodicLabelNoteIndices() const { return m_melodicLabelNoteIndices; }

private:
    std::array<int, MidiPitchCount> m_pitchNoteIndices;
    QVector<int> m_drumLaneNoteIndices;
    QVector<int> m_activePitches;
    QVector<int> m_activeDrumLanes;
    std::bitset<MidiPitchCount> m_labelPitches;
    QVector<int> m_melodicLabelNoteIndices;
};

} // namespace midi_play::visualization
