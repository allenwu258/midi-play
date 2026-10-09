#pragma once

// 动态、hairpin 和 tempo 在播放时间线上的预计算上下文。
#include "domain/music/playbacktimeline.h"

#include <QMap>
#include <memory>

namespace midi_play::playback {

// 预计算播放期间的速度、力度和渐强渐弱上下文。
// 所有时间均为反复展开后的输出微秒，供事件渲染器在实时路径外查询。
class PlaybackContext final {
public:
    // 从不可变文档和播放时间线投影动态、hairpin 与 tempo。
    PlaybackContext(std::shared_ptr<const music::MusicDocument> document,
                    std::shared_ptr<const music::PlaybackTimeline> timeline);
    // 返回指定轨道在时间点的力度；无匹配动态时使用 fallback。
    int velocityAt(const QString& trackId, qint64 timestampUs, int fallback) const;
    // 返回指定播放时间生效的 tempo，未命中时回退到 120 BPM。
    double tempoAt(qint64 timestampUs) const;

private:
    // 离散力度变化点。
    struct DynamicPoint { qint64 timestampUs; int velocity; };
    // 一个已投影到输出时间线的渐强/渐弱片段。
    struct HairpinPoint {
        qint64 startUs;
        qint64 endUs;
        double startProgress;
        double endProgress;
        bool crescendo;
        bool includeEnd;
    };
    QMap<QString, QVector<DynamicPoint>> m_dynamics;
    QMap<QString, QVector<HairpinPoint>> m_hairpins;
    QMap<qint64, double> m_tempos;
};

} // namespace midi_play::playback
