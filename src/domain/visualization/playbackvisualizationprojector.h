#pragma once

#include "domain/music/musicdocument.h"
#include "visualchart.h"

#include <memory>

namespace midi_play::visualization {

struct VisualizationProjectionOptions {
    int minimumPitchSpan = 24;
    int pitchGuardSemitones = 2;
    bool mergeTies = true;
    QString fallbackTitle;
};

class PlaybackVisualizationProjector final {
public:
    VisualChartPtr project(const music::MusicDocument& document,
                           quint64 generation,
                           const VisualizationProjectionOptions& options = {},
                           QString* error = nullptr) const;
};

} // namespace midi_play::visualization
