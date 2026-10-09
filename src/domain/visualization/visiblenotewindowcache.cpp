#include "visiblenotewindowcache.h"

// 实现带饱和边界的窗口缓存，按索引版本和包含关系控制刷新。
#include <algorithm>
#include <limits>

namespace midi_play::visualization {
namespace {

// amount 非负，窗口扩展时向最小时间饱和，避免有符号下溢。
VisualTime saturatedSubtract(VisualTime value, VisualTime amount)
{
    const auto minimum = std::numeric_limits<VisualTime>::min();
    return value < minimum + amount ? minimum : value - amount;
}

// amount 非负，窗口扩展时向最大时间饱和，避免有符号上溢。
VisualTime saturatedAdd(VisualTime value, VisualTime amount)
{
    const auto maximum = std::numeric_limits<VisualTime>::max();
    return value > maximum - amount ? maximum : value + amount;
}

} // namespace

// 缓存只依赖音乐时间窗口，不以墙钟或播放方向推断 seek。
bool VisibleNoteWindowCache::ensure(const VisibleNoteIndex& index,
                                    VisualTime exactStartUs, VisualTime exactEndUs,
                                    VisualTime guardUs)
{
    guardUs = std::max<VisualTime>(0, guardUs);
    if (exactEndUs < exactStartUs) {
        reset();
        return true;
    }

    // 同一索引对象重建也推进 revision，不能只比较对象地址。
    const bool indexChanged = m_index != &index || m_indexRevision != index.revision();
    const bool exactWindowContained = m_valid
        && exactStartUs >= m_cachedStartUs && exactEndUs <= m_cachedEndUs;
    if (!indexChanged && exactWindowContained) {
        return false;
    }

    m_index = &index;
    m_indexRevision = index.revision();
    m_cachedStartUs = saturatedSubtract(exactStartUs, guardUs);
    m_cachedEndUs = saturatedAdd(exactEndUs, guardUs);
    index.query(m_cachedStartUs, m_cachedEndUs, m_candidateNoteIndices);
    ++m_queryCount;
    m_valid = true;
    return true;
}

// 保留累计查询计数，丢弃所有可复用候选和区间边界。
void VisibleNoteWindowCache::reset()
{
    m_index = nullptr;
    m_indexRevision = 0;
    m_cachedStartUs = 0;
    m_cachedEndUs = -1;
    m_candidateNoteIndices.clear();
    m_valid = false;
}

} // namespace midi_play::visualization
