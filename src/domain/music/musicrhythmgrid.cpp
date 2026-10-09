#include "musicrhythmgrid.h"

#include <algorithm>
#include <cmath>

// MusicRhythmGrid 的拍号规范化、节奏细分和节拍点击生成实现。
namespace midi_play::music {
namespace {

constexpr int kMaximumPoints = 1'000'000;

// 按 tick 稳定排序变化，并让同一 tick 的最后一个值生效。
template<class T>
QVector<T> canonicalChanges(QVector<T> changes)
{
    std::stable_sort(changes.begin(), changes.end(), [](const T& a, const T& b) {
        return a.tick < b.tick;
    });
    QVector<T> result;
    for (const auto& change : changes) {
        if (change.tick < 0) continue;
        if (!result.isEmpty() && result.back().tick == change.tick) result.back() = change;
        else result.push_back(change);
    }
    return result;
}

// 返回严格晚于 tick 的第一个变化，用于确定当前位置生效的配置。
template<class T>
auto after(const QVector<T>& values, Tick tick)
{
    return std::upper_bound(values.cbegin(), values.cend(), tick,
        [](Tick value, const T& change) { return value < change.tick; });
}

// 判断两个拍号是否拥有相同的小节结构。
// MIDI metronome clocks 只改变点击间隔，不改变小节长度和相位。
bool sameMeter(const TimeSignatureChange& a, const TimeSignatureChange& b)
{
    // MIDI cc changes the click spacing, not the bar length or bar phase.
    return a.beats == b.beats && a.beatType == b.beatType
        && a.notated32nds == b.notated32nds && a.beatGroups == b.beatGroups;
}

// 把拍号的记谱分母换算为 PPQ tick。
// notated32nds 来自 MIDI 文件头的 32 分音符计数，不能固定假设为 8。
long double denominatorTicks(const TimeSignatureChange& signature)
{
    // SMF bb counts notated 32nds per MIDI quarter; it is not necessarily 8.
    return static_cast<long double>(MusicDocument::kPpq) * 32
        / (static_cast<long double>(signature.beatType) * signature.notated32nds);
}

struct Bar {
    // 待生成节奏点的小节半开区间及其弱起属性。
    Tick start;
    Tick end;
    int index;
    int number;
    bool pickup;
};

// 从音乐锚点计算每个周期的点位，而不是逐点累加四舍五入误差。
bool appendPattern(QVector<RhythmPoint>& points, const QVector<long double>& offsets,
                   long double period, long double anchor, Tick start, Tick end,
                   const Bar& bar, bool pickup, int& beatIndex)
{
    if (period < 1) return false;
    const auto firstCycle = static_cast<qint64>(std::max(0.0L,
        std::floor((start - anchor) / period)));
    for (qint64 cycle = firstCycle;; ++cycle) {
        const long double base = anchor + cycle * period;
        if (base >= static_cast<long double>(end) + 0.5L) break;
        for (long double offset : offsets) {
            const long double position = base + offset;
            if (position < static_cast<long double>(start) - 0.5L
                || position >= static_cast<long double>(end) - 0.5L) continue;
            const Tick tick = static_cast<Tick>(std::llround(position));
            if (tick < start || tick >= end) continue;
            // 恶意或异常拍号可能产生巨量点位；超过上限时让调用方
            // 将整个节奏网格标记为不可用，避免无限增长。
            if (points.size() >= kMaximumPoints) return false;
            points.push_back({tick, bar.index, bar.number, beatIndex++, tick == bar.start && !pickup});
        }
    }
    return true;
}

// 校验拍号/点击单位，并在每个小节区间内同时生成 subdivisions 和 clicks。
// written measures 使用导入的小节；没有真实小节时，MIDI 文档按拍号和
// format 2 sequence 起点构造占位小节。
bool buildGrid(const MusicDocument& document, const QVector<TimeSignatureChange>& signatures,
               QVector<RhythmPoint>& subdivisions, QVector<RhythmPoint>& clicks)
{
    const auto units = canonicalChanges(document.metronomeUnits());
    QVector<TimeSignatureChange> meters;
    for (const auto& signature : signatures) {
        if (signature.beats < 1 || signature.beats > 1024 || signature.beatType < 1
            || signature.beatType > 1024 || signature.notated32nds < 1
            || signature.notated32nds > 255 || signature.metronomeClocks < 0
            || signature.metronomeClocks > 255) return false;
        int sum = 0;
        for (int group : signature.beatGroups) {
            if (group < 1 || group > signature.beats) return false;
            sum += group;
        }
        if (!signature.beatGroups.isEmpty() && sum != signature.beats) return false;
        if (meters.isEmpty() || !sameMeter(meters.back(), signature)) meters.push_back(signature);
    }
    for (const auto& unit : units) {
        if (!std::isfinite(unit.quarterNotes) || unit.quarterNotes < 1.0 / 256
            || unit.quarterNotes > 256) return false;
    }

    auto emitBar = [&](const Bar& bar) {
        Tick sliceStart = bar.start;
        int subdivisionIndex = 0;
        int clickIndex = 0;
        while (sliceStart < bar.end) {
            const auto nextSignature = after(signatures, sliceStart);
            const auto& signature = *(nextSignature - 1);
            const auto nextUnit = after(units, sliceStart);
            Tick sliceEnd = bar.end;
            if (nextSignature != signatures.cend()) sliceEnd = std::min(sliceEnd, nextSignature->tick);
            if (nextUnit != units.cend()) sliceEnd = std::min(sliceEnd, nextUnit->tick);
            const long double notated = denominatorTicks(signature);
            const long double nominal = notated * signature.beats;
            const Tick meterStart = std::max(bar.start, (after(meters, sliceStart) - 1)->tick);
            const bool pickup = bar.pickup && meterStart == bar.start && bar.end - bar.start < nominal;
            // 弱起小节沿用完整小节的音乐锚点，避免第一个实际点被误判为
            // 正拍；后续重复段和节拍器因此共享同一相位。
            const long double anchor = pickup ? bar.end - nominal : meterStart;

            if (!appendPattern(subdivisions, {0}, notated, anchor, sliceStart, sliceEnd,
                               bar, pickup, subdivisionIndex)) return false;

            QVector<long double> pattern;
            long double period = 0;
            if (nextUnit == units.cbegin() && signature.metronomeClocks == 0
                && !signature.beatGroups.isEmpty()) {
                // 有加法拍号且没有更具体的点击单位时，按 beatGroups 生成
                // 例如 3+2/8 的重拍模式。
                for (int group : signature.beatGroups) {
                    pattern.push_back(period);
                    period += group * notated;
                }
            } else {
                pattern.push_back(0);
                if (nextUnit != units.cbegin()) {
                    // 显式 metronome unit 的优先级高于拍号默认点击单位。
                    period = (nextUnit - 1)->quarterNotes * MusicDocument::kPpq;
                } else if (signature.metronomeClocks > 0) {
                    // MIDI 文件的 24 MIDI clocks per quarter 规则。
                    period = static_cast<long double>(MusicDocument::kPpq) * signature.metronomeClocks / 24;
                } else {
                    // 复合拍默认按三拍一组，其余拍号按单拍点击。
                    period = notated * (signature.beats >= 6 && signature.beats % 3 == 0 ? 3 : 1);
                }
            }
            if (!appendPattern(clicks, pattern, period, anchor, sliceStart, sliceEnd,
                               bar, pickup, clickIndex)) return false;
            sliceStart = sliceEnd;
        }
        return true;
    };

    const bool hasWrittenMeasures = std::any_of(document.tracks().cbegin(), document.tracks().cend(),
        [](const Track& track) { return !track.measures.isEmpty(); });
    if (hasWrittenMeasures) {
        Tick previousEnd = 0;
        int index = 0;
        for (const auto& measure : document.measures()) {
            if (measure.start < previousEnd || measure.duration <= 0
                || measure.start > document.duration()
                || measure.duration > document.duration() - measure.start) return false;
            const Tick end = measure.start + measure.duration;
            const int number = measure.number > 0 ? measure.number : index + 1;
            if (!emitBar({measure.start, end, index, number, index == 0 && measure.implicit})) return false;
            previousEnd = end;
            if (++index > kMaximumPoints) return false;
        }
    } else {
        // MIDI 文档级小节通常只是占位信息，不能直接当作真实记谱小节。
        // 因此按拍号变化和独立 sequence 起点切分连续区域后再推导小节。
        QVector<Tick> boundaries {0, document.duration()};
        for (const auto& signature : meters) boundaries.push_back(signature.tick);
        boundaries += document.sequenceStarts();
        std::sort(boundaries.begin(), boundaries.end());
        boundaries.erase(std::unique(boundaries.begin(), boundaries.end()), boundaries.end());
        int index = 0;
        for (int region = 0; region + 1 < boundaries.size(); ++region) {
            const Tick origin = boundaries[region];
            const Tick limit = std::min(boundaries[region + 1], document.duration());
            if (origin < 0 || origin >= limit) continue;
            const auto& signature = *(after(meters, origin) - 1);
            const long double length = denominatorTicks(signature) * signature.beats;
            if (length < 1) return false;
            Tick start = origin;
            for (qint64 bar = 1; start < limit; ++bar) {
                const auto exactEnd = static_cast<long double>(origin) + bar * length;
                const Tick end = exactEnd >= limit ? limit : static_cast<Tick>(std::llround(exactEnd));
                if (end <= start || !emitBar({start, end, index, index + 1, false})) return false;
                if (++index > kMaximumPoints) return false;
                start = end;
            }
        }
    }
    return true;
}

} // namespace

// 汇总所有轨道拍号，建立默认 4/4，并根据文档时间基准生成节奏网格。
MusicRhythmGrid::MusicRhythmGrid(const MusicDocument& document)
{
    for (const auto& track : document.tracks()) m_signatures += track.timeSignatures;
    m_signatures = canonicalChanges(std::move(m_signatures));
    if (m_signatures.isEmpty() || m_signatures.front().tick > 0) m_signatures.push_front({0, 4, 4});
    if (!document.isValid()) {
        m_unavailableReason = QStringLiteral("请先加载乐曲");
    } else if (!document.hasMusicalTimebase()) {
        m_unavailableReason = QStringLiteral("此 MIDI 使用 SMPTE 时间码，无法可靠推导音乐节拍");
    } else if (!buildGrid(document, m_signatures, m_subdivisions, m_clicks)) {
        m_subdivisions.clear();
        m_clicks.clear();
        m_unavailableReason = QStringLiteral("乐曲拍号无效或节拍数量超过安全上限");
    } else if (m_clicks.isEmpty()) {
        m_unavailableReason = QStringLiteral("乐曲没有可播放的节拍");
    }
}

} // namespace midi_play::music
