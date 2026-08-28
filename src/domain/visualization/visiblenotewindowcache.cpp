#include "visiblenotewindowcache.h"

#include <algorithm>
#include <limits>

namespace midi_play::visualization {
namespace {

VisualTime saturatedSubtract(VisualTime value, VisualTime amount)
{
    const auto minimum = std::numeric_limits<VisualTime>::min();
    return value < minimum + amount ? minimum : value - amount;
}

VisualTime saturatedAdd(VisualTime value, VisualTime amount)
{
    const auto maximum = std::numeric_limits<VisualTime>::max();
    return value > maximum - amount ? maximum : value + amount;
}

} // namespace

bool VisibleNoteWindowCache::ensure(const VisibleNoteIndex& index,
                                    VisualTime exactStartUs, VisualTime exactEndUs,
                                    VisualTime guardUs)
{
    guardUs = std::max<VisualTime>(0, guardUs);
    if (exactEndUs < exactStartUs) {
        reset();
        return true;
    }

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
