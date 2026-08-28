#include "activenotelookup.h"

#include <algorithm>

namespace midi_play::visualization {

ActiveNoteLookup::ActiveNoteLookup()
{
    m_activePitches.reserve(MidiPitchCount);
    m_melodicLabelNoteIndices.reserve(MidiPitchCount);
    reset();
}

void ActiveNoteLookup::reset(qsizetype drumLaneCount)
{
    m_pitchNoteIndices.fill(-1);
    m_drumLaneNoteIndices.fill(-1, std::max<qsizetype>(0, drumLaneCount));
    m_activePitches.clear();
    m_activeDrumLanes.clear();
    m_activeDrumLanes.reserve(std::max<qsizetype>(0, drumLaneCount));
    m_labelPitches.reset();
    m_melodicLabelNoteIndices.clear();
}

void ActiveNoteLookup::add(int noteIndex, const VisualNote& note)
{
    if (noteIndex < 0) {
        return;
    }

    if (note.isPercussion()) {
        if (note.drumLane >= 0 && note.drumLane < m_drumLaneNoteIndices.size()) {
            if (m_drumLaneNoteIndices[note.drumLane] < 0) {
                m_activeDrumLanes.push_back(note.drumLane);
            }
            m_drumLaneNoteIndices[note.drumLane] = noteIndex;
        }
        return;
    }

    if (note.pitch < 0 || note.pitch >= MidiPitchCount) {
        return;
    }

    const auto pitchIndex = static_cast<std::size_t>(note.pitch);
    if (m_pitchNoteIndices[pitchIndex] < 0) {
        m_activePitches.push_back(note.pitch);
    }
    m_pitchNoteIndices[pitchIndex] = noteIndex;
    if (!note.simplifiedLabel.isEmpty() && !m_labelPitches.test(static_cast<std::size_t>(note.pitch))) {
        m_labelPitches.set(static_cast<std::size_t>(note.pitch));
        m_melodicLabelNoteIndices.push_back(noteIndex);
    }
}

int ActiveNoteLookup::noteIndexForPitch(int pitch) const
{
    return pitch >= 0 && pitch < MidiPitchCount
        ? m_pitchNoteIndices[static_cast<std::size_t>(pitch)]
        : -1;
}

int ActiveNoteLookup::noteIndexForDrumLane(int lane) const
{
    return lane >= 0 && lane < m_drumLaneNoteIndices.size()
        ? m_drumLaneNoteIndices[lane]
        : -1;
}

} // namespace midi_play::visualization
