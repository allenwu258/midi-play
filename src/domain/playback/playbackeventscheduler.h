#pragma once

// 主事件流与 NoteOff 事件流的增量调度接口。
#include "playbacktypes.h"
#include "itransporteventsdispatcher.h"

#include <functional>

namespace midi_play::playback {

// 按输出微秒时间向音频后端增量提供主事件和 NoteOff 事件。
class PlaybackEventScheduler final : public ITransportEventsDispatcher {
public:
    // 回调接收一个待提交事件；回调为空时不执行调度。
    using EventCallback = std::function<void(const PlaybackEvent&)>;

    // 从轨道数据重建全局主流和 NoteOff 流。
    void setTracks(const QVector<PlaybackData>* tracks) override;
    // 重置两个流的读取游标。
    void reset() override;
    // 将两个流的游标定位到目标时间。
    void seek(qint64 timestampUs) override;
    // 设置传输代次；窗口收集会校验该值。
    void setGeneration(quint64 generation) override { m_generation = generation; }
    // 收集并回调截至 timestampUs 的事件。
    void dispatch(qint64 timestampUs, const EventCallback& callback);
    // dispatch 的语义别名，便于调用方表达“推进到”操作。
    void dispatchUntil(qint64 timestampUs, const EventCallback& callback);
    // 返回截至目标时间的事件，并按协议优先级重新排序。
    QVector<PlaybackEvent> collectUntil(qint64 timestampUs);
    // 收集带代次校验的时间窗口。
    PlaybackEventWindow collectWindow(qint64 startUs, qint64 endUs,
                                      quint64 generation) override;

private:
    const QVector<PlaybackData>* m_tracks = nullptr;
    PlaybackEventMap m_mainStream;
    PlaybackEventMap m_offStream;
    int m_mainCursor = 0;
    int m_offCursor = 0;
    quint64 m_generation = 0;
};

} // namespace midi_play::playback
