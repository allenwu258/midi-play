#pragma once

#include "domain/visualization/visualchart.h"

#include <QString>

namespace midi_play::presentation {

struct PlaybackMetadata {
    QString key;
    QString timeSignature;
    QString tempo;

    bool operator==(const PlaybackMetadata&) const = default;
};

// Converts timeline metadata into presentation-ready, locale-neutral labels.
// The presenter is stateless so the window never needs to interpret score
// data or duplicate timeline lookup rules.
class PlaybackMetadataPresenter final {
public:
    static PlaybackMetadata at(const midi_play::visualization::VisualChart* chart, qint64 positionUs);
};

} // namespace midi_play::presentation
