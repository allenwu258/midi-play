#include "visiblenoteindex.h"

#include <algorithm>

namespace midi_play::visualization {

void VisibleNoteIndex::rebuild(const QVector<VisualNote>& notes)
{
    ++m_revision;
    m_notes = &notes;
    m_nodes.clear();
    m_nodes.reserve(notes.size());
    m_root = buildNode(notes, 0, notes.size());
}

int VisibleNoteIndex::buildNode(const QVector<VisualNote>& notes, int begin, int end)
{
    if (begin >= end) return -1;
    const int middle = begin + (end - begin) / 2;
    const int nodeIndex = m_nodes.size();
    m_nodes.push_back({middle, -1, -1, notes[middle].audibleEndUs});
    const int left = buildNode(notes, begin, middle);
    const int right = buildNode(notes, middle + 1, end);
    auto& node = m_nodes[nodeIndex];
    node.left = left;
    node.right = right;
    if (left >= 0) node.subtreeMaxEndUs = std::max(node.subtreeMaxEndUs, m_nodes[left].subtreeMaxEndUs);
    if (right >= 0) node.subtreeMaxEndUs = std::max(node.subtreeMaxEndUs, m_nodes[right].subtreeMaxEndUs);
    return nodeIndex;
}

void VisibleNoteIndex::query(VisualTime windowStartUs, VisualTime windowEndUs,
                             QVector<int>& result) const
{
    result.clear();
    if (!m_notes || m_root < 0 || windowEndUs < windowStartUs) return;
    queryNode(m_root, windowStartUs, windowEndUs, result);
}

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

    // Notes are start-time sorted. Everything in the right subtree starts no
    // earlier than this node, so it can be pruned once this start is too late.
    if (node.right >= 0 && note.startUs <= windowEndUs
        && m_nodes[node.right].subtreeMaxEndUs >= windowStartUs) {
        queryNode(node.right, windowStartUs, windowEndUs, result);
    }
}

} // namespace midi_play::visualization
