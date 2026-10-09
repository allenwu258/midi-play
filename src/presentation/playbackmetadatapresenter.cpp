// 元数据展示实现：统一调性命名、BPM 格式化，以及高频位置更新的区间缓存。
#include "playbackmetadatapresenter.h"

#include <QStringList>
#include <QtMath>

#include <algorithm>
#include <utility>

namespace midi_play::presentation {
namespace {

// 按五度数查表；非法五度数夹取至 ±7，minor 以不区分大小写方式识别。
QString keyName(int fifths, const QString& mode)
{
    static const QStringList majorNames {
        QStringLiteral("Cb"), QStringLiteral("Gb"), QStringLiteral("Db"), QStringLiteral("Ab"),
        QStringLiteral("Eb"), QStringLiteral("Bb"), QStringLiteral("F"), QStringLiteral("C"),
        QStringLiteral("G"), QStringLiteral("D"), QStringLiteral("A"), QStringLiteral("E"),
        QStringLiteral("B"), QStringLiteral("F#"), QStringLiteral("C#")
    };
    static const QStringList minorNames {
        QStringLiteral("Ab"), QStringLiteral("Eb"), QStringLiteral("Bb"), QStringLiteral("F"),
        QStringLiteral("C"), QStringLiteral("G"), QStringLiteral("D"), QStringLiteral("A"),
        QStringLiteral("E"), QStringLiteral("B"), QStringLiteral("F#"), QStringLiteral("C#"),
        QStringLiteral("G#"), QStringLiteral("D#"), QStringLiteral("A#")
    };
    const bool minor = mode.compare(QStringLiteral("minor"), Qt::CaseInsensitive) == 0;
    const int index = std::clamp(fifths, -7, 7) + 7;
    return (minor ? minorNames.at(index) : majorNames.at(index))
        + (minor ? QStringLiteral(" min") : QStringLiteral(" maj"));
}

} // namespace

PlaybackMetadata PlaybackMetadataPresenter::at(const midi_play::visualization::VisualChart* chart,
                                                qint64 positionUs)
{
    if (!chart) {
        return {QStringLiteral("--"), QStringLiteral("--/--"), QStringLiteral("-- BPM")};
    }

    // 查询共用同一有效音乐位置，速度四舍五入为整数 BPM。
    const qint64 resolvedPosition = std::clamp<qint64>(positionUs, 0, chart->durationUs());
    const auto key = chart->keyAt(resolvedPosition);
    const auto signature = chart->timeSignatureAt(resolvedPosition);
    return {
        keyName(key.fifths, key.mode),
        QStringLiteral("%1/%2").arg(signature.beats).arg(signature.beatType),
        QStringLiteral("%1 BPM").arg(qRound(chart->tempoAt(resolvedPosition)))
    };
}

void PlaybackMetadataTimeline::setChart(midi_play::visualization::VisualChartPtr chart)
{
    m_chart = std::move(chart);
    m_segments.clear();
    m_currentSegmentIndex = -1;
    if (!m_chart) return;

    // 三类元数据任一变化都可能切换标签；零点保证曲首存在展示区间。
    QVector<qint64> changePoints;
    changePoints.reserve(1 + m_chart->keys().size() + m_chart->timeSignatures().size()
                         + m_chart->tempos().size());
    changePoints.push_back(0);
    for (const auto& value : m_chart->keys()) changePoints.push_back(value.timeUs);
    for (const auto& value : m_chart->timeSignatures()) changePoints.push_back(value.timeUs);
    for (const auto& value : m_chart->tempos()) changePoints.push_back(value.timeUs);
    std::sort(changePoints.begin(), changePoints.end());
    changePoints.erase(std::unique(changePoints.begin(), changePoints.end()), changePoints.end());

    // 原始变化点可能不改变格式化结果，合并这些冗余区间。
    for (const qint64 timeUs : changePoints) {
        const auto metadata = PlaybackMetadataPresenter::at(m_chart.get(), timeUs);
        if (!m_segments.isEmpty() && m_segments.back().metadata == metadata) continue;
        m_segments.push_back({timeUs, metadata});
    }
}

void PlaybackMetadataTimeline::clear()
{
    m_chart.reset();
    m_segments.clear();
    m_currentSegmentIndex = -1;
}

bool PlaybackMetadataTimeline::update(qint64 positionUs, PlaybackMetadata& metadata)
{
    // 空图表只发布一次占位值，避免每次刷新重复更新 QLabel。
    if (m_segments.isEmpty()) {
        if (m_currentSegmentIndex == -2) return false;
        m_currentSegmentIndex = -2;
        metadata = PlaybackMetadataPresenter::at(nullptr, 0);
        return true;
    }

    const qint64 resolved = std::clamp<qint64>(
        positionUs, 0, m_chart ? m_chart->durationUs() : 0);
    // 连续播放通常停留在同一区间，先走常数时间的边界检查。
    if (m_currentSegmentIndex >= 0) {
        const auto& current = m_segments[m_currentSegmentIndex];
        const bool beforeNext = m_currentSegmentIndex + 1 >= m_segments.size()
            || resolved < m_segments[m_currentSegmentIndex + 1].startUs;
        if (resolved >= current.startUs && beforeNext) return false;
    }

    // upper_bound 的前一项是最后一个不晚于当前位置的变化点。
    const auto it = std::upper_bound(
        m_segments.cbegin(), m_segments.cend(), resolved,
        [](qint64 value, const Segment& segment) { return value < segment.startUs; });
    const int index = it == m_segments.cbegin()
        ? 0 : static_cast<int>(std::prev(it) - m_segments.cbegin());
    if (index == m_currentSegmentIndex) return false;
    m_currentSegmentIndex = index;
    metadata = m_segments[index].metadata;
    return true;
}

} // namespace midi_play::presentation
