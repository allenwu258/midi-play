#pragma once

#include "musicdocument.h"

#include <QVector>

#include <memory>

// 将 MusicDocument 的反复段预计算为实时播放可查询的不可变时间线。
namespace midi_play::music {

// 已定稿 MusicDocument 的不可变播放顺序投影。
//
// 构造时一次性完成反复展开、tempo map 初始化和段索引，实时播放线程
// 只进行二分查找，不重新解释乐谱导航标记。
class PlaybackTimeline final {
public:
    // document 为空时构造一个空时间线；非空文档在构造期间被视为只读。
    explicit PlaybackTimeline(std::shared_ptr<const MusicDocument> document);

    // 展开后的源区间列表。
    const QVector<PlaybackSegment>& segments() const { return m_segments; }
    // 输出时间线总长度，单位为 tick。
    Tick durationTicks() const { return m_durationTicks; }
    // 输出时间线总长度，单位为微秒。
    qint64 durationUs() const { return m_durationUs; }

    // 源文档 tick 到微秒的换算，不考虑反复展开。
    qint64 sourceTickToMicroseconds(Tick sourceTick) const;
    // 输出播放 tick 到微秒的换算，包含反复段的累计时长。
    qint64 outputTickToMicroseconds(Tick outputTick) const;

private:
    // 一个输出段在源时间和输出时间上的二分查找索引。
    struct SegmentIndex {
        Tick sourceStartTick = 0;
        Tick outputStartTick = 0;
        Tick outputEndTick = 0;
        qint64 outputStartUs = 0;
        qint64 sourceStartUs = 0;
    };

    std::shared_ptr<const MusicDocument> m_document;
    QVector<PlaybackSegment> m_segments;
    QVector<SegmentIndex> m_segmentIndex;
    Tick m_durationTicks = 0;
    qint64 m_durationUs = 0;
};

} // namespace midi_play::music
