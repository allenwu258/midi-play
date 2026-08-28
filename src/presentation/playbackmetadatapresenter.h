#pragma once

#include "domain/visualization/visualchart.h"

#include <QString>
#include <QVector>

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

// Preformats the union of key, time-signature and tempo change points. A
// stable segment costs one bounds check per UI frame; seeks use binary search.
class PlaybackMetadataTimeline final {
public:
    void setChart(midi_play::visualization::VisualChartPtr chart);
    void clear();
    bool update(qint64 positionUs, PlaybackMetadata& metadata);

    qsizetype segmentCount() const { return m_segments.size(); }
    int currentSegmentIndex() const { return m_currentSegmentIndex; }

private:
    struct Segment {
        qint64 startUs = 0;
        PlaybackMetadata metadata;
    };

    midi_play::visualization::VisualChartPtr m_chart;
    QVector<Segment> m_segments;
    int m_currentSegmentIndex = -1;
};

} // namespace midi_play::presentation
