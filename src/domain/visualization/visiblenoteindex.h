#pragma once

// 基于开始时间和子树最晚尾音时间的视觉区间查询索引。
#include "visualchart.h"

#include <QVector>

namespace midi_play::visualization {

// 平衡区间树以子树最晚结束时间剪枝，任意 seek 无需扫描所有已过去音符。
// 借用按开始时间排序的音符数组，其生命周期必须覆盖索引使用期。
class VisibleNoteIndex final {
public:
    // 创建空索引，后续可通过 rebuild 绑定图表。
    VisibleNoteIndex() = default;
    // 借用已有音符数组并立即建树。
    explicit VisibleNoteIndex(const QVector<VisualNote>& notes) { rebuild(notes); }

    // 重建树并推进 revision，使旧窗口缓存失效。
    void rebuild(const QVector<VisualNote>& notes);
    // 覆盖输出，返回与闭区间窗口相交的音符下标，保持开始时间顺序。
    void query(VisualTime windowStartUs, VisualTime windowEndUs,
               QVector<int>& result) const;
    // 判断是否有索引节点。
    bool isEmpty() const { return m_nodes.isEmpty(); }
    // 返回建树版本，供窗口缓存检测同一对象的内容变更。
    quint64 revision() const { return m_revision; }

private:
    // 节点只存下标与区间摘要，不复制 VisualNote。
    struct Node {
        int noteIndex = -1;
        int left = -1;
        int right = -1;
        VisualTime subtreeMaxEndUs = 0;
    };

    // 对 [begin, end) 的中点递归建树并汇总子树最晚结束时间。
    int buildNode(const QVector<VisualNote>& notes, int begin, int end);
    // 以开始时间和结束时间摘要双向剪枝。
    void queryNode(int nodeIndex, VisualTime windowStartUs, VisualTime windowEndUs,
                   QVector<int>& result) const;

    const QVector<VisualNote>* m_notes = nullptr;
    QVector<Node> m_nodes;
    int m_root = -1;
    quint64 m_revision = 0;
};

} // namespace midi_play::visualization
