#pragma once

#include "domain/visualization/visualchart.h"

#include <QString>
#include <QVector>

namespace midi_play::presentation::visualization {

struct PlaybackOverlayText {
    QString marker;
    QString lyric;

    bool operator==(const PlaybackOverlayText&) const = default;
};

// Precomputes the union of marker and lyric change points. Normal forward
// playback only checks the current segment bounds; seeks fall back to a
// binary search without carrying direction-dependent state.
class PlaybackOverlayTimeline final {
public:
    void setChart(const midi_play::visualization::VisualChartPtr& chart);
    const PlaybackOverlayText& at(qint64 positionUs);
    void clear();

    qsizetype segmentCount() const { return m_segments.size(); }
    int currentSegmentIndex() const { return m_currentSegmentIndex; }
    quint64 binarySearchCount() const { return m_binarySearchCount; }

private:
    struct Segment {
        qint64 startUs = 0;
        PlaybackOverlayText text;
    };

    static const PlaybackOverlayText& emptyText();

    midi_play::visualization::VisualChartPtr m_chart;
    QVector<Segment> m_segments;
    int m_currentSegmentIndex = -1;
    quint64 m_binarySearchCount = 0;
};

} // namespace midi_play::presentation::visualization
