#include "visualchart.h"

#include <algorithm>

namespace midi_play::visualization {
namespace {

template<typename Container, typename TimeGetter>
int itemIndexAt(const Container& values, VisualTime timeUs, TimeGetter timeGetter)
{
    if (values.isEmpty()) return -1;
    const auto it = std::upper_bound(values.cbegin(), values.cend(), timeUs,
                                     [&](VisualTime value, const auto& item) {
                                         return value < timeGetter(item);
                                     });
    return it == values.cbegin() ? 0 : static_cast<int>(std::prev(it) - values.cbegin());
}

} // namespace

double VisualChart::tempoAt(VisualTime timeUs) const
{
    const int index = itemIndexAt(m_tempos, timeUs, [](const auto& point) { return point.timeUs; });
    return index >= 0 ? m_tempos[index].bpm : 120.0;
}

VisualKeyPoint VisualChart::keyAt(VisualTime timeUs) const
{
    const int index = itemIndexAt(m_keys, timeUs, [](const auto& point) { return point.timeUs; });
    return index >= 0 ? m_keys[index] : VisualKeyPoint {};
}

VisualTimeSignaturePoint VisualChart::timeSignatureAt(VisualTime timeUs) const
{
    const int index = itemIndexAt(m_timeSignatures, timeUs, [](const auto& point) { return point.timeUs; });
    return index >= 0 ? m_timeSignatures[index] : VisualTimeSignaturePoint {};
}

QString VisualChart::markerAt(VisualTime timeUs) const
{
    if (m_markers.isEmpty() || timeUs < m_markers.front().timeUs) return {};
    const int index = itemIndexAt(m_markers, timeUs, [](const auto& marker) { return marker.timeUs; });
    return index >= 0 ? m_markers[index].text : QString {};
}

QString VisualChart::lyricAt(VisualTime timeUs) const
{
    if (m_lyrics.isEmpty() || timeUs < m_lyrics.front().timeUs) return {};
    const int index = itemIndexAt(m_lyrics, timeUs, [](const auto& lyric) { return lyric.timeUs; });
    return index >= 0 ? m_lyrics[index].text : QString {};
}

} // namespace midi_play::visualization
