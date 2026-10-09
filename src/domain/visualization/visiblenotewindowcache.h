#pragma once

// 在可见窗口外围保留保护区，减少连续播放中的区间树查询。
#include "visiblenoteindex.h"

#include <QVector>

namespace midi_play::visualization {

// 精确窗口仍被缓存区间包含时复用候选；seek 或索引变更按区间/版本立即刷新。
class VisibleNoteWindowCache final {
public:
    // 保证候选覆盖精确窗口，返回 true 表示缓存已刷新或重置。
    bool ensure(const VisibleNoteIndex& index,
                VisualTime exactStartUs, VisualTime exactEndUs,
                VisualTime guardUs);
    // 清空候选与索引绑定，不重置累计 queryCount。
    void reset();

    // 返回借用候选数组；下一次刷新/重置可能使引用其存储的 span 失效。
    const QVector<int>& candidateNoteIndices() const { return m_candidateNoteIndices; }
    // 返回带保护区的缓存起止位置。
    VisualTime cachedStartUs() const { return m_cachedStartUs; }
    VisualTime cachedEndUs() const { return m_cachedEndUs; }
    // 返回实际区间查询累计次数，便于衡量复用效果。
    quint64 queryCount() const { return m_queryCount; }
    // 判断当前缓存是否可参与包含关系检查。
    bool isValid() const { return m_valid; }

private:
    const VisibleNoteIndex* m_index = nullptr;
    quint64 m_indexRevision = 0;
    VisualTime m_cachedStartUs = 0;
    VisualTime m_cachedEndUs = -1;
    QVector<int> m_candidateNoteIndices;
    quint64 m_queryCount = 0;
    bool m_valid = false;
};

} // namespace midi_play::visualization
