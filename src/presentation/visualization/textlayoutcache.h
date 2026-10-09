#pragma once

// 文字整形与省略缓存，供 Qt 绘制及实时/离屏 Vulkan 文字图集生成复用。
#include <QFont>
#include <QHash>
#include <QStaticText>
#include <QString>

namespace midi_play::presentation::visualization {

// 显示用途参与缓存键，隔离各类标签的布局契约。
enum class TextLayoutRole : quint8 {
    Measure,
    Strike,
    Octave,
    Drum,
    Marker,
    Lyric
};

// 可直接绘制的 QStaticText，以及省略后文本和字体度量。
struct PreparedTextLayout {
    QString displayText;
    QStaticText staticText;
    qreal advance = 0.0;
    QSizeF size;
};

// 缓存键覆盖布局输入，以最近最少使用淘汰控制长乐曲的内存占用。
// 调用方须串行访问本缓存，本对象不提供跨线程同步。
class TextLayoutCache final {
public:
    // 最大项数至少为一，初始化时预留哈希容量。
    explicit TextLayoutCache(qsizetype maximumEntries = 1024);

    // 返回缓存借用引用；后续插入/淘汰/clear 后不应继续持有。
    const PreparedTextLayout& layout(TextLayoutRole role, const QString& text,
                                     const QFont& font, qreal maximumWidth = -1.0,
                                     qreal devicePixelRatio = 1.0);
    // 清空缓存与访问序号，保留累计构建次数。
    void clear();

    // 返回当前缓存项数。
    qsizetype size() const { return m_entries.size(); }
    // 返回实际整形构建次数。
    quint64 buildCount() const { return m_buildCount; }

private:
    // 宽度和 DPI 以 1/64 粒度量化，降低浮点抖动造成的重复项。
    struct Key {
        TextLayoutRole role = TextLayoutRole::Measure;
        QString text;
        QFont font;
        qint64 maximumWidth = -1;
        qint64 devicePixelRatio = 64;

        bool operator==(const Key&) const = default;

        // 哈希字段与相等比较一致，字体对象也参与整形身份。
        friend size_t qHash(const Key& key, size_t seed = 0) noexcept
        {
            return qHashMulti(seed, static_cast<quint8>(key.role), key.text, key.font,
                              key.maximumWidth, key.devicePixelRatio);
        }
    };

    // 访问序号用于寻找最近最少使用项。
    struct Entry {
        PreparedTextLayout layout;
        quint64 lastUse = 0;
    };

    // 规范化宽度与 DPI，构造完整缓存身份。
    static Key makeKey(TextLayoutRole role, const QString& text, const QFont& font,
                       qreal maximumWidth, qreal devicePixelRatio);
    // 仅在容量耗尽时线性寻找最旧项。
    void evictLeastRecentlyUsed();

    QHash<Key, Entry> m_entries;
    qsizetype m_maximumEntries = 1024;
    quint64 m_accessSequence = 0;
    quint64 m_buildCount = 0;
};

} // namespace midi_play::presentation::visualization
