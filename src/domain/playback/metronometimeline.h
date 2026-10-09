#pragma once

// 节拍器时间线和实时点击游标的领域接口。
#include "domain/music/musicdocument.h"
#include "domain/music/playbacktimeline.h"

#include <QVector>
#include <memory>
#include <optional>

namespace midi_play::playback {

// 节拍器点击的重音级别。
enum class MetronomeAccent {
    Beat,
    Measure
};

// 播放顺序中的一次节拍器发生事件。
struct MetronomeBeat {
    qint64 timeUs = 0;
    quint64 occurrenceId = 0;
    int measureIndex = -1;
    int beatIndex = 0;
    MetronomeAccent accent = MetronomeAccent::Beat;
};

// 按播放顺序保存的不可变节拍器事件。
// 构建在实时路径之外完成，并复用 MIDI 事件相同的反复展开时间线。
class MetronomeTimeline final {
public:
    // 从文档节奏网格和播放时间线构建点击事件。
    explicit MetronomeTimeline(std::shared_ptr<const music::MusicDocument> document,
                               std::shared_ptr<const music::PlaybackTimeline> timeline);

    // 返回按时间排序的点击事件。
    const QVector<MetronomeBeat>& beats() const { return m_beats; }
    // 返回第一个不早于目标时间的点击下标。
    int lowerBound(qint64 timeUs) const;
    // 判断节拍器时间线是否可用。
    bool available() const { return m_unavailableReason.isEmpty() && !m_beats.isEmpty(); }
    // 返回不可用原因；可用时为空字符串。
    const QString& unavailableReason() const { return m_unavailableReason; }

private:
    QVector<MetronomeBeat> m_beats;
    QString m_unavailableReason;
};

// 与 UI 刷新和音频队列解耦的节拍器传输游标。
// 发生卡顿时消费错过的区间，但最多发出一个仍然及时的点击。
class MetronomeScheduler final {
public:
    // 将游标定位到目标时间。
    void seek(const MetronomeTimeline& timeline, qint64 timeUs);
    // 消费截至目标时间的点击；过迟点击直接跳过。
    std::optional<MetronomeBeat> takeDue(const MetronomeTimeline& timeline,
                                       qint64 timeUs, double rate);
private:
    int m_cursor = 0;
};

} // namespace midi_play::playback
