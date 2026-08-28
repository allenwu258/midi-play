#pragma once

#include "visiblenoteindex.h"

#include <QVector>

namespace midi_play::visualization {

// Retains an expanded interval-query result while the exact render window
// moves inside it. Arbitrary seeks remain correct because containment, rather
// than elapsed wall time, decides when the cache must be refreshed.
class VisibleNoteWindowCache final {
public:
    bool ensure(const VisibleNoteIndex& index,
                VisualTime exactStartUs, VisualTime exactEndUs,
                VisualTime guardUs);
    void reset();

    const QVector<int>& candidateNoteIndices() const { return m_candidateNoteIndices; }
    VisualTime cachedStartUs() const { return m_cachedStartUs; }
    VisualTime cachedEndUs() const { return m_cachedEndUs; }
    quint64 queryCount() const { return m_queryCount; }
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
