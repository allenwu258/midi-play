#include "musicdocument.h"

#include <algorithm>
#include <cmath>

// MusicDocument 的时间、调性和反复展开算法实现。
// 本文件不负责读取具体文件格式，只消费已归一化的领域结构。
namespace midi_play::music {

// 将调号的五度圈值转换为 MIDI pitch class。minor 使用关系大调的
// 相对主音偏移，从而与后续音级推导保持一致。
int KeyContext::tonicPitchClass() const
{
    const int normalizedFifths = std::clamp(fifths, -7, 7);
    int tonic = (normalizedFifths * 7) % 12;
    if (tonic < 0) tonic += 12;
    if (mode.compare(QStringLiteral("minor"), Qt::CaseInsensitive) == 0) tonic = (tonic + 9) % 12;
    return tonic;
}

// 在当前调式音阶中寻找距离给定 MIDI 音高最近的音级。
// 这里返回的是 1..7 的功能音级，不负责决定具体的升降号拼写。
int KeyContext::degreeFor(const WrittenPitch& pitch) const
{
    static constexpr int majorScale[] = {0, 2, 4, 5, 7, 9, 11};
    static constexpr int minorScale[] = {0, 2, 3, 5, 7, 8, 10};
    const auto& scale = mode.compare(QStringLiteral("minor"), Qt::CaseInsensitive) == 0
        ? minorScale : majorScale;
    const int tonic = tonicPitchClass();
    const int pitchClass = ((pitch.midiPitch % 12) + 12) % 12;
    int bestDegree = 1;
    int bestDistance = 12;
    for (int degree = 0; degree < 7; ++degree) {
        const int candidate = (tonic + scale[degree]) % 12;
        const int directDistance = std::abs(pitchClass - candidate);
        const int distance = std::min(directDistance, 12 - directDistance);
        if (distance < bestDistance) {
            bestDistance = distance;
            bestDegree = degree + 1;
        }
    }
    return bestDegree;
}

// 将源小节按 repeat、ending、D.C.、D.S. 和 coda 标记展开为线性播放段。
// 每次循环只推进小节索引和跳转上下文，不复制音符数据。
RepeatList RepeatList::build(const QVector<Measure>& measures, Tick duration)
{
    RepeatList list;
    if (measures.isEmpty()) {
        list.m_segments.push_back({0, duration, 0, 0, -1});
        list.m_duration = duration;
        return list;
    }

    int segnoIndex = -1;
    int codaIndex = -1;
    for (int index = 0; index < measures.size(); ++index) {
        if (measures[index].segno && segnoIndex < 0) segnoIndex = index;
        if (measures[index].coda && codaIndex < 0) codaIndex = index;
    }

    RepeatJumpContext context;
    int index = 0;
    Tick output = 0;
    // 导航标记可能形成循环；步数上限限制异常输入的展开量，不改变正常小节的源数据。
    const int maxSteps = qMax(1024, measures.size() * 64);
    for (int step = 0; index >= 0 && index < measures.size() && step < maxSteps; ++step) {
        const auto& measure = measures[index];
        // 回到 repeat 起点时必须保留当前 pass；此处重置会导致每次到
        // repeatEnd 都重新从第一遍开始，最终只能依赖 maxSteps 截断。
        if (measure.repeatStart && context.repeatPass == 0) {
            context.repeatStartIndex = index;
        }

        // 未选中的房子不贡献输出时长，但仍处理其反复/跳转标记以推进导航状态。
        const bool inEnding = measure.endingNumber > 0;
        const bool endingSelected = !inEnding || measure.endingNumber == context.repeatPass + 1;
        if (endingSelected) {
            list.m_segments.push_back({measure.start, measure.start + measure.duration,
                                       output, context.repeatPass, index});
            output += measure.duration;
        }

        if (measure.repeatEnd) {
            const int repeatCount = qMax(1, measure.repeatCount);
            if (context.repeatPass + 1 < repeatCount) {
                ++context.repeatPass;
                index = context.repeatStartIndex;
                continue;
            }
            context.repeatStartIndex = index + 1;
            context.repeatPass = 0;
        }

        // To Coda/Fine 仅在 D.C./D.S. 回跳后的上下文生效，普通首遍继续顺序播放。
        if (context.codaArmed && measure.toCoda && codaIndex >= 0) {
            index = codaIndex;
            context.codaArmed = false;
            context.repeatStartIndex = index;
            context.repeatPass = 0;
            continue;
        }
        if ((context.daCapoTaken || context.dalSegnoTaken) && measure.fine) break;

        if (!context.daCapoTaken && measure.daCapo) {
            context.daCapoTaken = true;
            context.codaArmed = true;
            index = 0;
            context.repeatStartIndex = 0;
            context.repeatPass = 0;
            continue;
        }
        if (!context.dalSegnoTaken && measure.dalSegno && segnoIndex >= 0) {
            context.dalSegnoTaken = true;
            context.codaArmed = true;
            index = segnoIndex;
            context.repeatStartIndex = index;
            context.repeatPass = 0;
            continue;
        }
        ++index;
    }

    if (list.m_segments.isEmpty()) list.m_segments.push_back({0, duration, 0, 0, -1});
    // 最后一个输出段的末端就是展开时间线的总长度。
    const auto& last = list.m_segments.back();
    list.m_duration = last.outputStart + (last.sourceEnd - last.sourceStart);
    return list;
}

// 使用惰性构建的 tempo map 将源 tick 转换为微秒。
// tick 会先限制在 [0, duration]，避免外部拖动位置产生越界时间。
qint64 MusicDocument::tickToMicroseconds(Tick tick) const
{
    tick = std::clamp<Tick>(tick, 0, m_duration);
    rebuildTempoMap();
    if (m_tempoMap.isEmpty()) return static_cast<qint64>(std::llround(
        tick * 60'000'000.0 / (120.0 * kPpq)));
    const auto it = std::upper_bound(m_tempoMap.cbegin(), m_tempoMap.cend(), tick,
                                     [](Tick value, const TempoSegment& segment) {
                                         return value < segment.start;
                                     });
    const auto& segment = it == m_tempoMap.cbegin() ? m_tempoMap.front() : *std::prev(it);
    return segment.startUs + static_cast<qint64>(std::llround(
        (tick - segment.start) * 60'000'000.0 / (segment.bpm * kPpq)));
}

// 通过二分查找寻找第一个不早于目标微秒位置的 tick。
Tick MusicDocument::microsecondsToTick(qint64 microseconds) const
{
    if (microseconds <= 0) {
        return 0;
    }
    // 返回换算值不小于目标的最小 tick；逐段取整使反向换算不能直接除以当前 tempo。
    Tick low = 0;
    Tick high = m_duration;
    while (low < high) {
        const Tick mid = low + (high - low) / 2;
        if (tickToMicroseconds(mid) < microseconds) {
            low = mid + 1;
        } else {
            high = mid;
        }
    }
    return low;
}

// 把展开后的输出 tick 映射到对应 PlaybackSegment，再累计此前段落的实际源时长。
// 反复段重复使用同一源 tempo，但输出时间必须按每一段单独累计。
qint64 MusicDocument::playbackTickToMicroseconds(Tick outputTick) const
{
    outputTick = std::max<Tick>(0, outputTick);
    const auto segments = playbackSegments();
    if (segments.isEmpty()) {
        return tickToMicroseconds(outputTick);
    }

    qint64 elapsedUs = 0;
    for (const auto& segment : segments) {
        const Tick length = std::max<Tick>(0, segment.sourceEnd - segment.sourceStart);
        if (outputTick < segment.outputStart + length) {
            const Tick sourceTick = segment.sourceStart + (outputTick - segment.outputStart);
            return elapsedUs + (tickToMicroseconds(sourceTick) - tickToMicroseconds(segment.sourceStart));
        }
        elapsedUs += tickToMicroseconds(segment.sourceEnd) - tickToMicroseconds(segment.sourceStart);
    }
    return elapsedUs;
}

// 按 tick/sequence 排序并生成分段累计时间的 tempo map。
// 该函数是 const，因为缓存属于文档的惰性派生状态；修改 tempos() 会清空缓存。
void MusicDocument::rebuildTempoMap() const
{
    if (!m_tempoMap.isEmpty()) return;
    QVector<TempoChange> changes = m_tempos;
    std::sort(changes.begin(), changes.end(), [](const auto& left, const auto& right) {
        if (left.tick != right.tick) return left.tick < right.tick;
        return left.sequence < right.sequence;
    });
    double bpm = 120.0;
    Tick cursor = 0;
    qint64 elapsedUs = 0;
    for (const auto& change : changes) {
        const Tick tick = std::clamp<Tick>(change.tick, 0, m_duration);
        if (tick < cursor) continue;
        // 先累计旧 tempo 覆盖的区间，再把新 tempo 作为当前位置之后的值。
        elapsedUs += static_cast<qint64>(std::llround(
            (tick - cursor) * 60'000'000.0 / (bpm * kPpq)));
        bpm = change.bpm > 0.0 ? change.bpm : bpm;
        if (!m_tempoMap.isEmpty() && m_tempoMap.back().start == tick) {
            m_tempoMap.back().startUs = elapsedUs;
            m_tempoMap.back().bpm = bpm;
        } else {
            m_tempoMap.push_back({tick, m_duration, elapsedUs, bpm});
        }
        cursor = tick;
    }
    // 没有从 tick 0 开始的显式速度时，补入默认 120 BPM 基线。
    if (m_tempoMap.isEmpty() || m_tempoMap.front().start != 0) {
        m_tempoMap.push_front({0, m_duration, 0, 120.0});
    }
    for (int i = 0; i + 1 < m_tempoMap.size(); ++i) {
        m_tempoMap[i].end = m_tempoMap[i + 1].start;
    }
    m_tempoMap.back().end = m_duration;
}

// 从轨道小节表中选出最长的一份作为文档级网格。
// MIDI 文档可能没有真实小节，因此没有轨道小节时生成一个占位小节。
void MusicDocument::rebuildMeasureGrid()
{
    m_measures.clear();
    for (const auto& track : m_tracks) {
        if (track.measures.size() > m_measures.size()) m_measures = track.measures;
    }
    if (m_measures.isEmpty() && m_duration > 0) {
        m_measures.push_back({1, 0, m_duration});
    }
}

// 返回源小节的播放顺序投影；没有小节信息时视为单一连续段。
QVector<PlaybackSegment> MusicDocument::playbackSegments() const
{
    if (m_measures.isEmpty()) return {{0, m_duration, 0, 0, -1}};
    return RepeatList::build(m_measures, m_duration).segments();
}

// 计算反复展开后输出时间线的总 tick 数。
Tick MusicDocument::playbackDuration() const
{
    if (m_measures.isEmpty()) return m_duration;
    return RepeatList::build(m_measures, m_duration).duration();
}

} // namespace midi_play::music
