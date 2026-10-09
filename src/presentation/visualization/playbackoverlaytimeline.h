#pragma once

// 预计算标记与歌词的联合变化区间，供逐帧覆盖层查询。
#include "domain/visualization/visualchart.h"

#include <QString>
#include <QVector>

namespace midi_play::presentation::visualization {

// 一个时间区间内同时显示的标记与歌词文本。
struct PlaybackOverlayText {
    QString marker;
    QString lyric;

    bool operator==(const PlaybackOverlayText&) const = default;
};

// 当前区间覆盖目标时直接命中，否则二分查询；seek 不依赖历史播放方向。
class PlaybackOverlayTimeline final {
public:
    // 图表身份变化时重建合并区间，相同图表不重复构建。
    void setChart(const midi_play::visualization::VisualChartPtr& chart);
    // 限制时间到文档范围后返回有效文本，空时间线返回静态空对象。
    const PlaybackOverlayText& at(qint64 positionUs);
    // 清空图表、区间和诊断计数。
    void clear();

    // 查询区间数量、当前命中下标与二分次数，用于缓存诊断。
    qsizetype segmentCount() const { return m_segments.size(); }
    int currentSegmentIndex() const { return m_currentSegmentIndex; }
    quint64 binarySearchCount() const { return m_binarySearchCount; }

private:
    // 从 startUs 到下一变化点前的半开区间。
    struct Segment {
        qint64 startUs = 0;
        PlaybackOverlayText text;
    };

    // 静态空值保证空时间线返回引用的生命周期。
    static const PlaybackOverlayText& emptyText();

    midi_play::visualization::VisualChartPtr m_chart;
    QVector<Segment> m_segments;
    int m_currentSegmentIndex = -1;
    quint64 m_binarySearchCount = 0;
};

} // namespace midi_play::presentation::visualization
