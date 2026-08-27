#pragma once

#include "visualchart.h"

#include <QVector>

namespace midi_play::visualization {

// Balanced interval index. Each subtree stores the latest note end, allowing
// an arbitrary seek window to be queried without scanning elapsed notes.
class VisibleNoteIndex final {
public:
    VisibleNoteIndex() = default;
    explicit VisibleNoteIndex(const QVector<VisualNote>& notes) { rebuild(notes); }

    void rebuild(const QVector<VisualNote>& notes);
    void query(VisualTime windowStartUs, VisualTime windowEndUs,
               QVector<int>& result) const;
    bool isEmpty() const { return m_nodes.isEmpty(); }

private:
    struct Node {
        int noteIndex = -1;
        int left = -1;
        int right = -1;
        VisualTime subtreeMaxEndUs = 0;
    };

    int buildNode(const QVector<VisualNote>& notes, int begin, int end);
    void queryNode(int nodeIndex, VisualTime windowStartUs, VisualTime windowEndUs,
                   QVector<int>& result) const;

    const QVector<VisualNote>* m_notes = nullptr;
    QVector<Node> m_nodes;
    int m_root = -1;
};

} // namespace midi_play::visualization
