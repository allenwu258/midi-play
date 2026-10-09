#include "playbackeventscheduler.h"

// 实现按时间游标收集和按代次校验的播放事件调度。
namespace midi_play::playback {

// 从各轨道复制事件并重新分配全局序号，随后建立可二分定位的索引。
void PlaybackEventScheduler::setTracks(const QVector<PlaybackData>* tracks)
{
    m_tracks = tracks;
    m_mainStream.clear();
    m_offStream.clear();
    if (m_tracks) {
        quint64 sequence = 0;
        for (const auto& track : *m_tracks) {
            // 主流包含音符起音、控制器和程序变化；NoteOff 单独维护，
            // 以便暂停/跳转时可以独立冲刷。
            for (const auto& event : track.mainStream.events()) {
                auto copy = event;
                copy.sequence = sequence++;
                m_mainStream.insert(std::move(copy));
            }
            for (const auto& event : track.offStream.events()) {
                auto copy = event;
                copy.sequence = sequence++;
                m_offStream.insert(std::move(copy));
            }
        }
    }
    m_mainStream.finalize();
    m_offStream.finalize();
    reset();
}

// 将两个读取游标回到流首端，不改变已绑定的数据和代次。
void PlaybackEventScheduler::reset()
{
    m_mainCursor = 0;
    m_offCursor = 0;
}

// 使用 lowerBound 跳过目标时间之前已经处理过的事件。
void PlaybackEventScheduler::seek(qint64 timestampUs)
{
    m_mainCursor = m_mainStream.lowerBound(timestampUs);
    m_offCursor = m_offStream.lowerBound(timestampUs);
}

// 收集并逐个调用回调；事件排序和游标推进由 collectUntil 负责。
void PlaybackEventScheduler::dispatch(qint64 timestampUs, const EventCallback& callback)
{
    if (!callback) return;
    const auto events = collectUntil(timestampUs);
    for (const auto& event : events) {
        callback(event);
    }
}

// 保留旧接口命名，当前实现与 dispatch 完全一致。
void PlaybackEventScheduler::dispatchUntil(qint64 timestampUs, const EventCallback& callback)
{
    dispatch(timestampUs, callback);
}

// NoteOff 先入结果，再与主流合并排序，确保同一时间点的协议顺序稳定。
QVector<PlaybackEvent> PlaybackEventScheduler::collectUntil(qint64 timestampUs)
{
    QVector<PlaybackEvent> result;
    while (m_offCursor < m_offStream.size()
           && m_offStream.at(m_offCursor).timestampUs <= timestampUs) {
        result.push_back(m_offStream.at(m_offCursor++));
    }
    while (m_mainCursor < m_mainStream.size()
           && m_mainStream.at(m_mainCursor).timestampUs <= timestampUs) {
        result.push_back(m_mainStream.at(m_mainCursor++));
    }
    std::stable_sort(result.begin(), result.end(), [](const auto& left, const auto& right) {
        if (left.timestampUs != right.timestampUs) return left.timestampUs < right.timestampUs;
        const int leftPriority = playbackEventPriority(left);
        const int rightPriority = playbackEventPriority(right);
        if (leftPriority != rightPriority) return leftPriority < rightPriority;
        return left.sequence < right.sequence;
    });
    return result;
}

// 代次不匹配或窗口逆序时返回空窗口，防止旧 transport 事件重新进入后端。
PlaybackEventWindow PlaybackEventScheduler::collectWindow(qint64 startUs, qint64 endUs,
                                                          quint64 generation)
{
    PlaybackEventWindow window;
    window.startUs = startUs;
    window.endUs = endUs;
    window.generation = generation;
    if (generation != m_generation || window.endUs < startUs) return window;
    window.events = collectUntil(window.endUs);
    return window;
}

} // namespace midi_play::playback
