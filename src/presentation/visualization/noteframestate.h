#pragma once

// 从当前音乐时间计算音符几何、琴键照明、光晕及密度透明度。
#include "domain/visualization/activenotelookup.h"
#include "domain/visualization/playbackscenestate.h"
#include "noterendercache.h"

#include <QRectF>
#include <QVector>
#include <array>

namespace midi_play::presentation::visualization {

// 单个音符在本帧的体、尾、头矩形及释放淡出系数。
struct NoteGeometry {
    QRectF body;
    QRectF tail;
    QRectF head;
    qreal opacity = 1;
};

// 将绝对音符时间映射到击键线附近的垂直位置。
NoteGeometry noteGeometry(const PreparedNoteRenderData& note,
                          const PlaybackSceneGeometry& geometry, qint64 positionUs);
// 按限定强度混合琴键底色与材质亮色。
QColor illuminatedKeyColor(const QColor& base, const QColor& light, qreal strength);

// 同键竞争后选出的代表音符及持续/攻击照明强度。
struct KeyIllumination {
    int noteIndex = -1;
    qreal strength = 0;
    qreal attack = 0;
};

// 本帧短暂光晕或火花的几何与颜色。
struct NoteGlow {
    QRectF rect;
    QColor color;
};

// 瞬态效果直接由音乐时间求值，不逐帧积分；丢帧、seek 和后端切换不累积效果。
class NoteFrameState final {
public:
    // 重建本帧可见/活动状态，暂停时保留音符几何统计但不生成击键效果。
    void prepare(const midi_play::visualization::PlaybackSceneState& state,
                 const PlaybackSceneGeometry& geometry, const NoteRenderCache& cache);
    // 返回合法音高或鼓道的照明数据，调用方保证下标范围。
    const KeyIllumination& key(int pitch) const { return m_keys.at(size_t(pitch)); }
    const KeyIllumination& drum(int lane) const { return m_drums.at(lane); }
    // 返回本帧有界光效列表。
    const QVector<NoteGlow>& glows() const { return m_glows; }
    // 返回播放态中按可听区间判定的活动音符查找表，非播放态为空。
    const midi_play::visualization::ActiveNoteLookup& active() const { return m_active; }
    // 返回按可见音符覆盖面积衰减的整体体透明度。
    qreal bodyOpacity() const { return m_bodyOpacity; }
    // 返回精确窗口内的可见数，以及播放态中当前可听区间内的活动数。
    qsizetype visibleCount() const { return m_visibleCount; }
    qsizetype activeCount() const { return m_activeCount; }

private:
    std::array<KeyIllumination, 128> m_keys;
    QVector<KeyIllumination> m_drums;
    QVector<NoteGlow> m_glows;
    midi_play::visualization::ActiveNoteLookup m_active;
    qreal m_bodyOpacity = 1;
    qsizetype m_visibleCount = 0;
    qsizetype m_activeCount = 0;
};

} // namespace midi_play::presentation::visualization
