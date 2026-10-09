#pragma once

// Qt 与 GPU 共用的音符材质及去重键。
#include "domain/visualization/visualchart.h"
#include "noteappearance.h"

#include <QColor>
#include <QHashFunctions>

namespace midi_play::presentation::visualization {

// 后端无关材质，颜色为未预乘 alpha 的 sRGB；各后端在输出边界只预乘一次。
struct NoteMaterial {
    QColor body;
    QColor head;
    QColor tail;
    QColor keyFill;
    QColor keyTop;
    QColor glow;
    float energy = 0.0f;
};

// 两种配色共用完整样式身份，重新配色不改变音符到样式的映射。
// 此键须包含所有影响材质的音符属性，主题/配色由缓存统一失效处理。
struct NoteStyleKey {
    int trackIndex;
    int pitch;
    int voiceTint;
    int velocityBucket;
    bool ghost;
    bool percussion;
    bool operator==(const NoteStyleKey&) const = default;
};

// 哈希字段与默认相等比较保持一致。
inline size_t qHash(const NoteStyleKey& key, size_t seed = 0) noexcept
{
    return qHashMulti(seed, key.trackIndex, key.pitch, key.voiceTint,
                      key.velocityBucket, key.ghost, key.percussion);
}

// 将音符身份、音高、声部色偏和力度分桶转换为去重键。
NoteStyleKey noteMaterialKey(const midi_play::visualization::VisualNote& note);
// 按轨道配色与外观策略生成体、头、尾、琴键和光晕颜色。
NoteMaterial makeNoteMaterial(const midi_play::visualization::VisualChart& chart,
                              const midi_play::visualization::VisualNote& note,
                              const NoteAppearance& appearance = noteAppearanceFor());

// 与 note.* 着色器公式对应的几何及音乐时间常量，修改时须保持两条路径一致。
inline constexpr qreal kNoteMinimumHeight = 4.0;
inline constexpr qreal kNoteHeadHeight = 2.5;
inline constexpr qreal kNoteTailWidth = 0.38;
inline constexpr qint64 kNoteReleaseUs = 240'000;
inline constexpr qint64 kNoteAttackUs = 180'000;

} // namespace midi_play::presentation::visualization
