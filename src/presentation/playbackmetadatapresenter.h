#pragma once

// 播放元数据展示：将图表中的调号、拍号与速度转换为标签，并缓存变化区间。

#include "domain/visualization/visualchart.h"

#include <QString>
#include <QVector>

namespace midi_play::presentation {

// 三个标签的值对象，相等比较用于合并显示内容相同的相邻区间。
struct PlaybackMetadata {
    QString key;
    QString timeSignature;
    QString tempo;

    bool operator==(const PlaybackMetadata&) const = default;
};

// 无状态格式化器：集中处理时间线查询和标签格式，主窗口只接收展示值。
class PlaybackMetadataPresenter final {
public:
    // 位置夹取到曲目范围；图表为空时返回占位文字。
    static PlaybackMetadata at(const midi_play::visualization::VisualChart* chart, qint64 positionUs);
};

// 预格式化三类变化点的并集；区间内更新只检查边界，跨区间时执行二分查找。
class PlaybackMetadataTimeline final {
public:
    // 替换图表并重建合并后的标签区间，下一次更新会发布当前值。
    void setChart(midi_play::visualization::VisualChartPtr chart);
    // 释放图表和区间缓存，重置发布状态。
    void clear();
    // 仅在展示区间发生变化时写入 metadata 并返回 true。
    bool update(qint64 positionUs, PlaybackMetadata& metadata);

    qsizetype segmentCount() const { return m_segments.size(); }
    int currentSegmentIndex() const { return m_currentSegmentIndex; }

private:
    // 标签在 [startUs, 下一段 startUs) 内保持不变。
    struct Segment {
        qint64 startUs = 0;
        PlaybackMetadata metadata;
    };

    midi_play::visualization::VisualChartPtr m_chart;
    QVector<Segment> m_segments;
    // -1 表示尚未发布，-2 表示已发布空图表占位值，非负值为当前区间。
    int m_currentSegmentIndex = -1;
};

} // namespace midi_play::presentation
