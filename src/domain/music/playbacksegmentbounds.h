#pragma once

#include "musicdocument.h"

// 播放段连续性辅助函数，供播放和可视化共同判断持续音边界。
namespace midi_play::music {

// 计算每个播放段的连续可听结束位置。
//
// 小节边界本身不等于音符释放；只有反复、跳转或结尾造成的输出时间线
// 不连续时，持续事件才必须在当前段截断。
inline QVector<Tick> continuousPlaybackEnds(const QVector<PlaybackSegment>& segments)
{
    QVector<Tick> ends(segments.size());
    for (qsizetype i = segments.size(); i-- > 0;) {
        const auto& segment = segments[i];
        ends[i] = segment.sourceEnd;
        // 只有源段首尾相接且输出段也连续时，持续音才可以跨过当前段
        // 延续到后一个段；反复跳转会打断这一连续性。
        if (i + 1 < segments.size()
            && segment.sourceEnd == segments[i + 1].sourceStart
            && segment.outputStart + segment.sourceEnd - segment.sourceStart == segments[i + 1].outputStart)
            ends[i] = ends[i + 1];
    }
    return ends;
}

} // namespace midi_play::music
