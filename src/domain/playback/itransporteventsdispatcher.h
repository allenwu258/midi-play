#pragma once

// 播放传输事件调度器的抽象接口及批量时间窗口值对象。
#include "playbacktypes.h"

namespace midi_play::playback {

// 一次调度窗口及其代次。events 覆盖 endUs 之前应提交的事件。
struct PlaybackEventWindow {
    qint64 startUs = 0;
    qint64 endUs = 0;
    quint64 generation = 0;
    QVector<PlaybackEvent> events;
};

class ITransportEventsDispatcher {
public:
    // 接口析构必须为虚函数，以便会话安全管理具体调度器。
    virtual ~ITransportEventsDispatcher() = default;
    // 绑定由 PlaybackModel 持有的轨道数据。
    virtual void setTracks(const QVector<PlaybackData>* tracks) = 0;
    // 将调度游标恢复到流起点。
    virtual void reset() = 0;
    // 将调度游标定位到目标时间。
    virtual void seek(qint64 timestampUs) = 0;
    // 更新传输代次，使旧窗口失效。
    virtual void setGeneration(quint64 generation) = 0;
    // 收集指定时间窗口内的事件，generation 不匹配时返回空窗口。
    virtual PlaybackEventWindow collectWindow(qint64 startUs, qint64 endUs,
                                               quint64 generation) = 0;
};

} // namespace midi_play::playback
