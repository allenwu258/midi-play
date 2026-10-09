#include "visualchart.h"

// 实现按微秒查询视觉图表中的音乐状态和文本。
#include <algorithm>

namespace midi_play::visualization {
namespace {

// 有序数组中找最后一个不晚于目标的项；早于首项时选首项，空数组返回 -1。
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

// 音乐状态从首项向前延伸，缺失数据才使用默认值。
double VisualChart::tempoAt(VisualTime timeUs) const
{
    const int index = itemIndexAt(m_tempos, timeUs, [](const auto& point) { return point.timeUs; });
    return index >= 0 ? m_tempos[index].bpm : 120.0;
}

// 按值返回有效调号。
VisualKeyPoint VisualChart::keyAt(VisualTime timeUs) const
{
    const int index = itemIndexAt(m_keys, timeUs, [](const auto& point) { return point.timeUs; });
    return index >= 0 ? m_keys[index] : VisualKeyPoint {};
}

// 按值返回有效拍号。
VisualTimeSignaturePoint VisualChart::timeSignatureAt(VisualTime timeUs) const
{
    const int index = itemIndexAt(m_timeSignatures, timeUs, [](const auto& point) { return point.timeUs; });
    return index >= 0 ? m_timeSignatures[index] : VisualTimeSignaturePoint {};
}

// 文字只在实际到达后显示，不向首项之前延伸。
QString VisualChart::markerAt(VisualTime timeUs) const
{
    if (m_markers.isEmpty() || timeUs < m_markers.front().timeUs) return {};
    const int index = itemIndexAt(m_markers, timeUs, [](const auto& marker) { return marker.timeUs; });
    return index >= 0 ? m_markers[index].text : QString {};
}

// 同时刻多项由 upper_bound 选中最后一项。
QString VisualChart::lyricAt(VisualTime timeUs) const
{
    if (m_lyrics.isEmpty() || timeUs < m_lyrics.front().timeUs) return {};
    const int index = itemIndexAt(m_lyrics, timeUs, [](const auto& lyric) { return lyric.timeUs; });
    return index >= 0 ? m_lyrics[index].text : QString {};
}

} // namespace midi_play::visualization
