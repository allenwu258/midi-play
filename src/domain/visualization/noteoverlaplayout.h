#pragma once

#include "visualchart.h"

namespace midi_play::visualization {

// Assign stable lanes for overlapping key holds. Pedal-only tails do not
// displace later attacks. Lane identities remain fixed for the entire note.
void assignNoteOverlapLanes(QVector<VisualNote>& notes);

} // namespace midi_play::visualization
