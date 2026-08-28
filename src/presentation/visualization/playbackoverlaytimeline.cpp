#include "playbackoverlaytimeline.h"

#include <algorithm>
#include <limits>

namespace midi_play::presentation::visualization {

void PlaybackOverlayTimeline::setChart(
    const midi_play::visualization::VisualChartPtr& chart)
{
    if (m_chart.get() == chart.get()) return;

    clear();
    m_chart = chart;
    if (!m_chart) return;

    const auto& markers = m_chart->markers();
    const auto& lyrics = m_chart->lyrics();
    qsizetype markerIndex = 0;
    qsizetype lyricIndex = 0;
    PlaybackOverlayText current;

    while (markerIndex < markers.size() && markers[markerIndex].timeUs <= 0) {
        current.marker = markers[markerIndex++].text;
    }
    while (lyricIndex < lyrics.size() && lyrics[lyricIndex].timeUs <= 0) {
        current.lyric = lyrics[lyricIndex++].text;
    }
    m_segments.push_back({0, current});

    while (markerIndex < markers.size() || lyricIndex < lyrics.size()) {
        const qint64 markerTime = markerIndex < markers.size()
            ? markers[markerIndex].timeUs : std::numeric_limits<qint64>::max();
        const qint64 lyricTime = lyricIndex < lyrics.size()
            ? lyrics[lyricIndex].timeUs : std::numeric_limits<qint64>::max();
        const qint64 timeUs = std::min(markerTime, lyricTime);

        while (markerIndex < markers.size() && markers[markerIndex].timeUs == timeUs) {
            current.marker = markers[markerIndex++].text;
        }
        while (lyricIndex < lyrics.size() && lyrics[lyricIndex].timeUs == timeUs) {
            current.lyric = lyrics[lyricIndex++].text;
        }
        if (current == m_segments.back().text) continue;
        m_segments.push_back({timeUs, current});
    }
}

const PlaybackOverlayText& PlaybackOverlayTimeline::at(qint64 positionUs)
{
    if (m_segments.isEmpty()) return emptyText();

    const qint64 resolved = std::clamp<qint64>(
        positionUs, 0, m_chart ? m_chart->durationUs() : 0);
    if (m_currentSegmentIndex >= 0) {
        const auto& current = m_segments[m_currentSegmentIndex];
        const bool beforeNext = m_currentSegmentIndex + 1 >= m_segments.size()
            || resolved < m_segments[m_currentSegmentIndex + 1].startUs;
        if (resolved >= current.startUs && beforeNext) return current.text;
    }

    ++m_binarySearchCount;
    const auto it = std::upper_bound(
        m_segments.cbegin(), m_segments.cend(), resolved,
        [](qint64 value, const Segment& segment) { return value < segment.startUs; });
    m_currentSegmentIndex = it == m_segments.cbegin()
        ? 0 : static_cast<int>(std::prev(it) - m_segments.cbegin());
    return m_segments[m_currentSegmentIndex].text;
}

void PlaybackOverlayTimeline::clear()
{
    m_chart.reset();
    m_segments.clear();
    m_currentSegmentIndex = -1;
    m_binarySearchCount = 0;
}

const PlaybackOverlayText& PlaybackOverlayTimeline::emptyText()
{
    static const PlaybackOverlayText empty;
    return empty;
}

} // namespace midi_play::presentation::visualization
