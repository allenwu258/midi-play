#pragma once

#include "musicdocument.h"

namespace midi_play::music {

struct RhythmPoint {
    Tick tick = 0;
    int measureIndex = -1;
    int measureNumber = 0;
    int beatIndex = 0;
    bool downbeat = false;
};

// Immutable source-score rhythm, before repeat expansion. Both projections
// consume the same meter, bar boundaries and pickup phase. Subdivisions use
// the notated denominator; clicks use explicit units, MIDI clocks or grouping.
// Neither projection may independently infer bars from PPQ/time signatures.
class MusicRhythmGrid final {
public:
    explicit MusicRhythmGrid(const MusicDocument& document);

    const QVector<RhythmPoint>& subdivisions() const { return m_subdivisions; }
    const QVector<RhythmPoint>& clicks() const { return m_clicks; }
    const QVector<TimeSignatureChange>& signatures() const { return m_signatures; }
    bool available() const { return m_unavailableReason.isEmpty(); }
    const QString& unavailableReason() const { return m_unavailableReason; }

private:
    QVector<RhythmPoint> m_subdivisions;
    QVector<RhythmPoint> m_clicks;
    QVector<TimeSignatureChange> m_signatures;
    QString m_unavailableReason;
};

} // namespace midi_play::music
