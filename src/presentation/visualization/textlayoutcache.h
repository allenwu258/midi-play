#pragma once

#include <QFont>
#include <QHash>
#include <QStaticText>
#include <QString>

namespace midi_play::presentation::visualization {

enum class TextLayoutRole : quint8 {
    Measure,
    Strike,
    Octave,
    Drum,
    Marker,
    Lyric
};

struct PreparedTextLayout {
    QString displayText;
    QStaticText staticText;
    qreal advance = 0.0;
    QSizeF size;
};

// GUI-thread cache for shaped text. Keys include every input that can affect
// glyph layout; least-recently-used eviction keeps long scores bounded.
class TextLayoutCache final {
public:
    explicit TextLayoutCache(qsizetype maximumEntries = 1024);

    const PreparedTextLayout& layout(TextLayoutRole role, const QString& text,
                                     const QFont& font, qreal maximumWidth = -1.0,
                                     qreal devicePixelRatio = 1.0);
    void clear();

    qsizetype size() const { return m_entries.size(); }
    quint64 buildCount() const { return m_buildCount; }

private:
    struct Key {
        TextLayoutRole role = TextLayoutRole::Measure;
        QString text;
        QFont font;
        qint64 maximumWidth = -1;
        qint64 devicePixelRatio = 64;

        bool operator==(const Key&) const = default;

        friend size_t qHash(const Key& key, size_t seed = 0) noexcept
        {
            return qHashMulti(seed, static_cast<quint8>(key.role), key.text, key.font,
                              key.maximumWidth, key.devicePixelRatio);
        }
    };

    struct Entry {
        PreparedTextLayout layout;
        quint64 lastUse = 0;
    };

    static Key makeKey(TextLayoutRole role, const QString& text, const QFont& font,
                       qreal maximumWidth, qreal devicePixelRatio);
    void evictLeastRecentlyUsed();

    QHash<Key, Entry> m_entries;
    qsizetype m_maximumEntries = 1024;
    quint64 m_accessSequence = 0;
    quint64 m_buildCount = 0;
};

} // namespace midi_play::presentation::visualization
