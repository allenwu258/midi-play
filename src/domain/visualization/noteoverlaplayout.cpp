#include "noteoverlaplayout.h"

#include <algorithm>
#include <array>

namespace midi_play::visualization {

void assignNoteOverlapLanes(QVector<VisualNote>& notes)
{
    std::array<QVector<int>, 256> pitches;
    for (int i = 0; i < notes.size(); ++i) {
        const auto& note = notes[i];
        if (note.pitch >= 0 && note.pitch < 128)
            pitches[size_t(note.pitch + (note.isPercussion() ? 128 : 0))].push_back(i);
    }
    for (const auto& indices : pitches) {
        QVector<VisualTime> laneEnds;
        QVector<int> component;
        VisualTime componentEnd = -1;
        const auto finish = [&] {
            for (int index : component) notes[index].coincidentCount = int(laneEnds.size());
            component.clear();
            laneEnds.clear();
        };
        for (int index : indices) {
            auto& note = notes[index];
            if (note.startUs >= componentEnd) finish();
            auto available = std::find_if(laneEnds.begin(), laneEnds.end(),
                [&](VisualTime end) { return end <= note.startUs; });
            if (available == laneEnds.end() && laneEnds.size() < 4) {
                note.coincidentIndex = int(laneEnds.size());
                laneEnds.push_back(note.keyEndUs);
            } else {
                if (available == laneEnds.end()) available = std::min_element(laneEnds.begin(), laneEnds.end());
                note.coincidentIndex = int(available - laneEnds.begin());
                *available = std::max(*available, note.keyEndUs);
            }
            componentEnd = std::max(componentEnd, note.keyEndUs);
            component.push_back(index);
        }
        finish();
    }
}

} // namespace midi_play::visualization
