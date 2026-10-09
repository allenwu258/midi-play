#include "textlayoutcache.h"

// 实现文字省略、QStaticText 预整形和有界 LRU 淘汰。
#include <QFontMetricsF>
#include <QTransform>

#include <algorithm>
#include <limits>
#include <utility>

namespace midi_play::presentation::visualization {

// 预留容量，避免常用标签集反复触发哈希扩容。
TextLayoutCache::TextLayoutCache(qsizetype maximumEntries)
    : m_maximumEntries(std::max<qsizetype>(1, maximumEntries))
{
    m_entries.reserve(m_maximumEntries);
}

// 命中只更新访问序号；未命中时先腾容量，再生成可绘制布局。
const PreparedTextLayout& TextLayoutCache::layout(
    TextLayoutRole role, const QString& text, const QFont& font,
    qreal maximumWidth, qreal devicePixelRatio)
{
    ++m_accessSequence;
    // 序号回绕时重置所有访问年龄，继续使用非零序号。
    if (m_accessSequence == 0) {
        for (auto it = m_entries.begin(); it != m_entries.end(); ++it) it->lastUse = 0;
        m_accessSequence = 1;
    }

    const Key key = makeKey(role, text, font, maximumWidth, devicePixelRatio);
    auto existing = m_entries.find(key);
    if (existing != m_entries.end()) {
        existing->lastUse = m_accessSequence;
        return existing->layout;
    }

    if (m_entries.size() >= m_maximumEntries) evictLeastRecentlyUsed();

    const QFontMetricsF metrics(font);
    const QString displayText = maximumWidth >= 0.0
        ? metrics.elidedText(text, Qt::ElideRight, maximumWidth)
        : text;
    PreparedTextLayout prepared;
    prepared.displayText = displayText;
    prepared.advance = metrics.horizontalAdvance(displayText);
    prepared.staticText.setText(displayText);
    // 标签始终按纯文本处理，避免歌词中的标记被误解释为富文本。
    prepared.staticText.setTextFormat(Qt::PlainText);
    prepared.staticText.setPerformanceHint(QStaticText::AggressiveCaching);
    prepared.staticText.prepare(QTransform(), font);
    prepared.size = prepared.staticText.size();

    Entry entry;
    entry.layout = std::move(prepared);
    entry.lastUse = m_accessSequence;
    auto inserted = m_entries.insert(key, std::move(entry));
    ++m_buildCount;
    return inserted->layout;
}

// 构建计数是累计诊断值，不随清空重置。
void TextLayoutCache::clear()
{
    m_entries.clear();
    m_accessSequence = 0;
}

// 负宽度表示不省略，DPI 至少为一再量化。
TextLayoutCache::Key TextLayoutCache::makeKey(TextLayoutRole role, const QString& text,
                                              const QFont& font, qreal maximumWidth,
                                              qreal devicePixelRatio)
{
    return {
        role,
        text,
        font,
        maximumWidth < 0.0 ? -1 : qRound64(maximumWidth * 64.0),
        qRound64(std::max<qreal>(1.0, devicePixelRatio) * 64.0)
    };
}

// 淘汰最小访问序号项，不要求额外链表维护。
void TextLayoutCache::evictLeastRecentlyUsed()
{
    auto victim = m_entries.end();
    quint64 oldestUse = std::numeric_limits<quint64>::max();
    for (auto it = m_entries.begin(); it != m_entries.end(); ++it) {
        if (it->lastUse < oldestUse) {
            oldestUse = it->lastUse;
            victim = it;
        }
    }
    if (victim != m_entries.end()) m_entries.erase(victim);
}

} // namespace midi_play::presentation::visualization
