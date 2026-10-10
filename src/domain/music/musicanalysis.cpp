// 音乐分析实现：补充调性、量化和演奏派生信息，并规范轨道顺序以支持播放及可视化。
#include "musicanalysis.h"
#include <QHash>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace midi_play::music {
namespace {

// 将任意 MIDI 音高归一化到 0..11 的 pitch class。
int pitchClass(int pitch)
{
    const int value = pitch % 12;
    return value < 0 ? value + 12 : value;
}

// 在当前调式中寻找最接近的音级及升降号数量。
// 结果用于简谱/记谱展示，不会修改原始 MIDI pitch。
int nearestScaleStep(int pitch, const KeyContext& key, int* alter)
{
    static constexpr int majorScale[] = {0, 2, 4, 5, 7, 9, 11};
    static constexpr int minorScale[] = {0, 2, 3, 5, 7, 8, 10};
    const auto& scale = key.mode.compare(QStringLiteral("minor"), Qt::CaseInsensitive) == 0
        ? minorScale : majorScale;
    const int pc = pitchClass(pitch);
    int bestStep = 0;
    int bestAlter = 0;
    int bestCost = std::numeric_limits<int>::max();
    for (int step = 0; step < 7; ++step) {
        const int natural = (key.tonicPitchClass() + scale[step]) % 12;
        int candidateAlter = pc - natural;
        while (candidateAlter > 6) candidateAlter -= 12;
        while (candidateAlter < -6) candidateAlter += 12;
        if (std::abs(candidateAlter) > 2) continue;

        // 优先选择升降号最少的调内拼写；等距时沿调号方向选择，
        // 使升号调倾向升记号、降号调倾向降记号。
        const int directionPenalty = candidateAlter == 0 ? 0
            : (key.fifths < 0 ? (candidateAlter > 0 ? 1 : 0)
                              : (candidateAlter < 0 ? 1 : 0));
        const int cost = std::abs(candidateAlter) * 10 + directionPenalty;
        if (cost < bestCost) {
            bestCost = cost;
            bestStep = step;
            bestAlter = candidateAlter;
        }
    }
    if (alter) *alter = bestAlter;
    return bestStep;
}

// 在有序小节表中查找包含 tick 的小节索引。
int findMeasure(const QVector<Measure>& measures, Tick tick)
{
    if (measures.isEmpty()) return -1;
    auto it = std::upper_bound(measures.cbegin(), measures.cend(), tick,
                               [](Tick value, const Measure& measure) { return value < measure.start; });
    if (it == measures.cbegin()) return 0;
    --it;
    const int index = static_cast<int>(it - measures.cbegin());
    return tick < it->start + it->duration ? index
                                           : std::min(index + 1, static_cast<int>(measures.size() - 1));
}

} // namespace

// 按“调性 -> 量化网格 -> 音高/小节派生 -> 和弦/连音 -> 三连音 ->
// lane -> swing”顺序执行分析。顺序不可随意交换：后续步骤依赖前面的派生字段。
MusicAnalysisReport MusicAnalyzer::analyze(MusicDocument& document, int laneCount) const
{
    MusicAnalysisReport report;
    report.estimatedKey = estimateKey(document, &report.keyConfidence);
    document.setKeyContext(report.estimatedKey);
    report.quantizationGrid = chooseQuantizationGrid(document, &report.meanQuantizationError);
    enrichPitchAndGrid(document, report.estimatedKey, report.quantizationGrid);
    buildChordsAndTies(document);
    report.tuplets = detectTuplets(document, report.quantizationGrid);
    report.lanes = assignLanes(document, std::max(1, laneCount));
    report.swingRatio = estimateSwingRatio(document, report.quantizationGrid);
    report.humanPerformance = report.meanQuantizationError > report.quantizationGrid * 0.1;
    return report;
}

// 估计调性：存在显式调号时直接采用，否则使用音高时值加权直方图
// 与 Krumhansl 风格的大小调 profile 进行相关性比较。
KeyContext MusicAnalyzer::estimateKey(const MusicDocument& document, double* confidence)
{
    if (!document.keySignatures().isEmpty()) {
        if (confidence) *confidence = 1.0;
        return {document.keySignatures().front().fifths, document.keySignatures().front().mode};
    }
    static constexpr double majorProfile[12] = {6.35, 2.23, 3.48, 2.33, 4.38, 4.09,
                                                 2.52, 5.19, 2.39, 3.66, 2.29, 2.88};
    static constexpr double minorProfile[12] = {6.33, 2.68, 3.52, 5.38, 2.60, 3.53,
                                                 2.54, 4.75, 3.98, 2.69, 3.34, 3.17};
    std::array<double, 12> histogram{};
    for (const auto& track : document.tracks()) {
        for (const auto& note : track.notes) {
            if (note.rest) continue;
            // 时值越长、力度越大的音符对调性估计贡献越大。
            histogram[pitchClass(note.pitch)] += std::max<Tick>(1, note.duration)
                * std::max(1, note.velocity) / 127.0;
        }
    }
    double best = -std::numeric_limits<double>::infinity();
    double second = best;
    int bestTonic = 0;
    bool bestMinor = false;
    for (int tonic = 0; tonic < 12; ++tonic) {
        for (int mode = 0; mode < 2; ++mode) {
            const auto& profile = mode == 0 ? majorProfile : minorProfile;
            double score = 0.0;
            for (int i = 0; i < 12; ++i) score += histogram[(i + tonic) % 12] * profile[i];
            if (score > best) {
                second = best;
                best = score;
                bestTonic = tonic;
                bestMinor = mode == 1;
            } else if (score > second) {
                second = score;
            }
        }
    }
    // 最优与次优候选的间隔作为置信度；没有有效音符时返回 0。
    if (confidence) *confidence = best > 0.0 ? std::clamp((best - second) / best, 0.0, 1.0) : 0.0;
    // 从五度圈寻找主音匹配的表示；等音调可能有多种调号，此处采用遍历遇到的首个表示。
    int fifths = 0;
    for (int candidate = -7; candidate <= 7; ++candidate) {
        KeyContext context {candidate, bestMinor ? QStringLiteral("minor") : QStringLiteral("major")};
        if (context.tonicPitchClass() == bestTonic) { fifths = candidate; break; }
    }
    return {fifths, bestMinor ? QStringLiteral("minor") : QStringLiteral("major")};
}

// 在一组预设 PPQ 网格中寻找平均起音误差最小者。
// 当误差接近时保留更粗网格，避免把普通演奏误判为过细量化。
Tick MusicAnalyzer::chooseQuantizationGrid(const MusicDocument& document, double* meanError)
{
    static constexpr Tick candidates[] = {240, 160, 120, 80, 60, 40, 30};
    Tick bestGrid = candidates[0];
    double bestError = std::numeric_limits<double>::infinity();
    for (const Tick grid : candidates) {
        double error = 0.0;
        int count = 0;
        for (const auto& track : document.tracks()) {
            for (const auto& note : track.notes) {
                if (note.rest) continue;
                const Tick nearest = static_cast<Tick>(std::llround(static_cast<double>(note.start) / grid)) * grid;
                error += std::abs(static_cast<double>(note.start - nearest));
                ++count;
            }
        }
        if (count > 0) error /= count;
        // 误差只有明显下降时才接受更细网格，从而保留较稳定的粗网格。
        if (error < bestError * 0.9 || bestError == std::numeric_limits<double>::infinity()) {
            bestError = error;
            bestGrid = grid;
        }
    }
    if (meanError) *meanError = std::isfinite(bestError) ? bestError : 0.0;
    return bestGrid;
}

// 为每个音符补齐绝对时间、音级、记谱音高、小节位置和量化网格位置。
void MusicAnalyzer::enrichPitchAndGrid(MusicDocument& document, const KeyContext& key, Tick grid)
{
    const auto& measures = document.measures();
    // 五度圈每前进一次对应一个七声音级步长；minor 再移动到关系小调。
    int tonicStep = ((key.fifths * 4) % 7 + 7) % 7;
    if (key.mode.compare(QStringLiteral("minor"), Qt::CaseInsensitive) == 0) {
        tonicStep = (tonicStep + 5) % 7;
    }
    for (auto& track : document.tracks()) {
        for (auto& note : track.notes) {
            // rawStart/rawEnd 保留源时间，供元数据和调试使用；播放时间
            // 仍由 PlaybackTimeline 按反复展开重新计算。
            note.rawStart = {note.start, document.tickToMicroseconds(note.start)};
            note.rawEnd = {note.start + note.duration, document.tickToMicroseconds(note.start + note.duration)};
            int scaleAlter = 0;
            const int scaleStep = nearestScaleStep(note.pitch, key, &scaleAlter);
            if (!note.hasWrittenPitch) {
                note.writtenPitch.midiPitch = note.pitch;
                note.writtenPitch.octave = note.pitch / 12 - 1;
                note.writtenPitch.step = (tonicStep + scaleStep) % 7;
                note.writtenPitch.alter = scaleAlter;
            } else {
                note.writtenPitch.midiPitch = note.pitch;
            }
            note.scaleDegree = scaleStep + 1;
            note.accidental = scaleAlter;
            note.measureIndex = findMeasure(measures, note.start);
            note.beat = note.measureIndex >= 0
                ? static_cast<double>(note.start - measures[note.measureIndex].start) / MusicDocument::kPpq + 1.0 : 0.0;
            // 量化只生成派生 GridTime，不修改 note.start/note.duration。
            const Tick gridStart = grid > 0
                ? static_cast<Tick>(std::llround(static_cast<double>(note.start) / grid)) * grid
                : note.start;
            const Tick gridEndRaw = note.start + note.duration;
            const Tick gridEnd = grid > 0
                ? static_cast<Tick>(std::llround(static_cast<double>(gridEndRaw) / grid)) * grid
                : gridEndRaw;
            // 量化后位置可能跨小节，重新定位网格小节，不能沿用原始 note.measureIndex。
            const int gridMeasure = findMeasure(measures, gridStart);
            const double gridBeat = gridMeasure >= 0
                ? static_cast<double>(gridStart - measures[gridMeasure].start) / MusicDocument::kPpq + 1.0 : 0.0;
            note.gridStart = {gridStart, gridMeasure, gridBeat};
            note.gridEnd = {gridEnd, findMeasure(measures, gridEnd),
                            gridBeat + static_cast<double>(gridEnd - gridStart) / MusicDocument::kPpq};
        }
    }
}

// 按起始 tick、voice 和 staff 分组构建和弦，并把 tie 链和长音抽取为
// 可供播放/可视化直接使用的辅助结构。
void MusicAnalyzer::buildChordsAndTies(MusicDocument& document)
{
    quint64 nextChord = 1;
    quint64 nextTie = 1;
    for (auto& track : document.tracks()) {
        track.chords.clear();
        track.holds.clear();
        track.ties.clear();
        track.drumMap.clear();
        // 后续和弦、tie 扫描依赖稳定的时间/声部/音高顺序。
        std::sort(track.notes.begin(), track.notes.end(), [](const auto& left, const auto& right) {
            if (left.start != right.start) return left.start < right.start;
            if (left.voice != right.voice) return left.voice < right.voice;
            return left.pitch < right.pitch;
        });
        for (int i = 0; i < track.notes.size();) {
            auto& first = track.notes[i];
            ChordGroup chord;
            chord.id = nextChord++;
            chord.start = first.start;
            chord.duration = first.duration;
            chord.voice = first.voice;
            chord.staff = first.staff;
            int j = i;
            while (j < track.notes.size() && track.notes[j].start == chord.start
                   && track.notes[j].voice == chord.voice && track.notes[j].staff == chord.staff) {
                track.notes[j].chordId = chord.id;
                chord.duration = std::max(chord.duration, track.notes[j].duration);
                chord.noteIds.push_back(track.notes[j].noteId);
                ++j;
            }
            track.chords.push_back(chord);
            i = j;
        }
        for (auto& note : track.notes) {
            // 仅把超过八分之一四分音符的音符视为可视化 hold 候选。
            if (note.duration > MusicDocument::kPpq / 8) track.holds.push_back({note.noteId, note.start,
                                                                 note.start + note.duration, -1});
            if (track.percussion && std::none_of(track.drumMap.cbegin(), track.drumMap.cend(),
                                                 [&](const auto& drum) { return drum.pitch == note.pitch; })) {
                track.drumMap.push_back({note.pitch, QStringLiteral("GM-%1").arg(note.pitch), note.pitch, 0});
            }
        }
        QHash<QString, quint64> openTies;
        for (int i = 0; i < track.notes.size(); ++i) {
            auto& note = track.notes[i];
            if (!note.tieStart && !note.tieStop) continue;
            const QString key = QStringLiteral("%1/%2/%3").arg(note.staff).arg(note.voice).arg(note.pitch);
            // staff/voice/pitch 组成 tie 的匹配键，避免不同声部的同音高
            // 互相合并。
            // 起止标记同时存在时延续当前链；孤立 tieStop 建立独立组，不向前猜测匹配音符。
            if (note.tieStop && openTies.contains(key)) {
                note.tieGroupId = openTies.value(key);
            } else {
                note.tieGroupId = nextTie++;
                track.ties.push_back({note.tieGroupId, {}});
            }
            auto tieIt = std::find_if(track.ties.begin(), track.ties.end(), [&](const auto& tie) {
                return tie.id == note.tieGroupId;
            });
            if (tieIt != track.ties.end()) tieIt->noteIds.push_back(note.noteId);
            if (note.tieStart) openTies.insert(key, note.tieGroupId);
            else openTies.remove(key);
        }
    }
}

// 用三个相邻的不同起音位置匹配 3:2 的间隔模式，返回启发式三连音候选。
QVector<TupletCandidate> MusicAnalyzer::detectTuplets(const MusicDocument& document, Tick grid)
{
    QVector<TupletCandidate> result;
    if (grid <= 0) return result;
    for (const auto& track : document.tracks()) {
        QVector<Tick> starts;
        for (const auto& note : track.notes) starts.push_back(note.start);
        std::sort(starts.begin(), starts.end());
        // 同时起音的和弦只贡献一个时间点，避免零间隔参与三连音判断。
        starts.erase(std::unique(starts.begin(), starts.end()), starts.end());
        for (int i = 0; i + 2 < starts.size(); ++i) {
            const Tick first = starts[i];
            const Tick last = starts[i + 2];
            const Tick span = last - first;
            for (const Tick base : std::array<Tick, 3>{grid, MusicDocument::kPpq / 2,
                                                        MusicDocument::kPpq}) {
                const Tick expected = base * 2;
                const Tick interval = starts[i + 1] - first;
                const Tick expectedInterval = base * 2 / 3;
                // 同时约束总跨度和中间间隔，降低普通切分被误判为三连音的概率。
                if (std::abs(span - expected) <= base / 4
                    && std::abs(interval - expectedInterval) <= base / 6) {
                    result.push_back({first, last + std::max<Tick>(1, expectedInterval), 3, 2,
                                      1.0 - std::min(1.0, std::abs(interval - expectedInterval)
                                                     / static_cast<double>(std::max<Tick>(1, base)))});
                    break;
                }
            }
        }
    }
    return result;
}

// 根据每条轨道的音域把音符压缩到 laneCount 个离散通道，并同步更新 hold。
QVector<LaneAssignment> MusicAnalyzer::assignLanes(MusicDocument& document, int laneCount)
{
    QVector<LaneAssignment> result;
    for (auto& track : document.tracks()) {
        int minPitch = 127;
        int maxPitch = 0;
        for (const auto& note : track.notes) {
            minPitch = std::min(minPitch, note.pitch);
            maxPitch = std::max(maxPitch, note.pitch);
        }
        const int range = std::max(1, maxPitch - minPitch + 1);
        for (auto& note : track.notes) {
            note.lane = std::clamp((note.pitch - minPitch) * laneCount / range, 0, laneCount - 1);
            result.push_back({note.noteId, note.lane});
        }
        for (auto& hold : track.holds) {
            const auto it = std::find_if(track.notes.cbegin(), track.notes.cend(), [&](const auto& note) {
                return note.noteId == hold.noteId;
            });
            if (it != track.notes.cend()) hold.lane = it->lane;
        }
    }
    return result;
}

// 收集全曲相邻起音间隔，估计长短拍比例；没有稳定长短关系时返回 1.0。
double MusicAnalyzer::estimateSwingRatio(const MusicDocument& document, Tick grid)
{
    if (grid <= 0) return 1.0;
    QVector<Tick> starts;
    for (const auto& track : document.tracks()) for (const auto& note : track.notes) starts.push_back(note.start);
    std::sort(starts.begin(), starts.end());
    starts.erase(std::unique(starts.begin(), starts.end()), starts.end());
    QVector<double> ratios;
    for (int i = 0; i + 2 < starts.size(); i += 2) {
        const Tick longPart = starts[i + 1] - starts[i];
        const Tick shortPart = starts[i + 2] - starts[i + 1];
        if (shortPart > 0 && longPart > shortPart) {
            const double ratio = static_cast<double>(longPart) / shortPart;
            if (ratio >= 1.2 && ratio <= 3.0) ratios.push_back(ratio);
        }
    }
    if (ratios.isEmpty()) return 1.0;
    double sum = 0.0;
    for (const double ratio : ratios) sum += ratio;
    return sum / ratios.size();
}

} // namespace midi_play::music
