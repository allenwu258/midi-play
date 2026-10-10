#pragma once

// 播放时间线接口：缓存反复展开区间及源/输出时间映射，供多个只读消费者共享。
#include "musicdocument.h"

#include <QVector>

#include <memory>

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
        Tick sourceStartTick = 0; // 源文档区间起点，同一源区间可对应多个输出段。
        Tick outputStartTick = 0; // 输出半开区间起点。
        Tick outputEndTick = 0; // 输出半开区间终点，恰好落在此处时查询后继段。
        qint64 outputStartUs = 0; // 此段之前所有输出段累计的微秒时长。
        qint64 sourceStartUs = 0; // 源 tempo map 上的段起点，用于消除源绝对时间偏移。
    };

    // 保持定稿文档存活；构建后外部不能修改其时间相关字段和惰性缓存。
    std::shared_ptr<const MusicDocument> m_document;
    QVector<PlaybackSegment> m_segments;
    QVector<SegmentIndex> m_segmentIndex;
    Tick m_durationTicks = 0;
    qint64 m_durationUs = 0;
};

} // namespace midi_play::music
