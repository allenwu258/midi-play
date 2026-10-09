#include "visiblenoteindex.h"

// 实现视觉音符区间树的构建和带尾音摘要的窗口剪枝。
#include <algorithm>

namespace midi_play::visualization {

// 数组必须有序且保持有效，索引使用期间不能修改其布局。
void VisibleNoteIndex::rebuild(const QVector<VisualNote>& notes)
{
    ++m_revision;
    m_notes = &notes;
    m_nodes.clear();
    m_nodes.reserve(notes.size());
    m_root = buildNode(notes, 0, notes.size());
}

// 中点分割形成平衡树，父节点摘要包含左右子树的最晚可听结束。
int VisibleNoteIndex::buildNode(const QVector<VisualNote>& notes, int begin, int end)
{
    if (begin >= end) return -1;
    const int middle = begin + (end - begin) / 2;
    const int nodeIndex = m_nodes.size();
    m_nodes.push_back({middle, -1, -1, notes[middle].audibleEndUs});
    const int left = buildNode(notes, begin, middle);
    const int right = buildNode(notes, middle + 1, end);
    // 递归追加可能改变向量存储，完成子树后再按下标取得父节点引用。
    auto& node = m_nodes[nodeIndex];
    node.left = left;
    node.right = right;
    if (left >= 0) node.subtreeMaxEndUs = std::max(node.subtreeMaxEndUs, m_nodes[left].subtreeMaxEndUs);
    if (right >= 0) node.subtreeMaxEndUs = std::max(node.subtreeMaxEndUs, m_nodes[right].subtreeMaxEndUs);
    return nodeIndex;
}

// 非法窗口与空索引都返回空结果，不保留上次查询的数据。
void VisibleNoteIndex::query(VisualTime windowStartUs, VisualTime windowEndUs,
                             QVector<int>& result) const
{
    result.clear();
    if (!m_notes || m_root < 0 || windowEndUs < windowStartUs) return;
    queryNode(m_root, windowStartUs, windowEndUs, result);
}

// 中序遍历保持结果顺序，完全早于窗口的子树立即退出。
void VisibleNoteIndex::queryNode(int nodeIndex, VisualTime windowStartUs,
                                 VisualTime windowEndUs, QVector<int>& result) const
{
    if (nodeIndex < 0) return;
    const auto& node = m_nodes[nodeIndex];
    if (node.subtreeMaxEndUs < windowStartUs) return;

    if (node.left >= 0 && m_nodes[node.left].subtreeMaxEndUs >= windowStartUs) {
        queryNode(node.left, windowStartUs, windowEndUs, result);
    }

    const auto& note = m_notes->at(node.noteIndex);
    if (note.startUs <= windowEndUs && note.audibleEndUs >= windowStartUs) {
        result.push_back(node.noteIndex);
    }

    // 主数组按开始时间排序；当前节点已经晚于窗口时，整个右子树都可跳过。
    if (node.right >= 0 && note.startUs <= windowEndUs
        && m_nodes[node.right].subtreeMaxEndUs >= windowStartUs) {
        queryNode(node.right, windowStartUs, windowEndUs, result);
    }
}

} // namespace midi_play::visualization
