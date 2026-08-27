#include "musicanalysis.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace midi_play::music {
namespace {

int pitchClass(int pitch)
{
    const int value = pitch % 12;
    return value < 0 ? value + 12 : value;
}

int nearestScaleStep(int pitch, const KeyContext& key, int* alter)
{
    static constexpr int majorScale[] = {0, 2, 4, 5, 7, 9, 11};
    static constexpr int minorScale[] = {0, 2, 3, 5, 7, 8, 10};
    const auto& scale = key.mode.compare(QStringLiteral("minor"), Qt::CaseInsensitive) == 0
        ? minorScale : majorScale;
    const int pc = pitchClass(pitch);
    int bestStep = 0;
    int bestAlter = 0;
    int bestDistance = std::numeric_limits<int>::max();
    for (int step = 0; step < 7; ++step) {
        const int natural = (key.tonicPitchClass() + scale[step]) % 12;
        for (int candidateAlter = -2; candidateAlter <= 2; ++candidateAlter) {
            int candidate = (natural + candidateAlter) % 12;
            if (candidate < 0) candidate += 12;
            int distance = std::abs(pc - candidate);
            distance = std::min(distance, 12 - distance);
            if (distance < bestDistance) {
                bestDistance = distance;
                bestStep = step;
                bestAlter = candidateAlter;
            }
        }
    }
    if (alter) *alter = bestAlter;
    return bestStep;
}

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
    if (confidence) *confidence = best > 0.0 ? std::clamp((best - second) / best, 0.0, 1.0) : 0.0;
    int fifths = 0;
    for (int candidate = -7; candidate <= 7; ++candidate) {
        KeyContext context {candidate, bestMinor ? QStringLiteral("minor") : QStringLiteral("major")};
        if (context.tonicPitchClass() == bestTonic) { fifths = candidate; break; }
    }
    return {fifths, bestMinor ? QStringLiteral("minor") : QStringLiteral("major")};
}

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
        // Prefer a coarser grid when errors are statistically equivalent.
        if (error < bestError * 0.9 || bestError == std::numeric_limits<double>::infinity()) {
            bestError = error;
            bestGrid = grid;
        }
    }
    if (meanError) *meanError = std::isfinite(bestError) ? bestError : 0.0;
    return bestGrid;
}

void MusicAnalyzer::enrichPitchAndGrid(MusicDocument& document, const KeyContext& key, Tick grid)
{
    const auto& measures = document.measures();
    for (auto& track : document.tracks()) {
        for (auto& note : track.notes) {
            note.rawStart = {note.start, document.tickToMicroseconds(note.start)};
            note.rawEnd = {note.start + note.duration, document.tickToMicroseconds(note.start + note.duration)};
            if (!note.hasWrittenPitch) {
                note.writtenPitch.midiPitch = note.pitch;
                note.writtenPitch.octave = note.pitch / 12 - 1;
                note.writtenPitch.step = nearestScaleStep(note.pitch, key, &note.writtenPitch.alter);
            } else {
                note.writtenPitch.midiPitch = note.pitch;
            }
            note.scaleDegree = key.degreeFor(note.writtenPitch);
            note.accidental = note.writtenPitch.alter;
            note.measureIndex = findMeasure(measures, note.start);
            note.beat = note.measureIndex >= 0
                ? static_cast<double>(note.start - measures[note.measureIndex].start) / MusicDocument::kPpq + 1.0 : 0.0;
            const Tick gridStart = grid > 0
                ? static_cast<Tick>(std::llround(static_cast<double>(note.start) / grid)) * grid
                : note.start;
            const Tick gridEndRaw = note.start + note.duration;
            const Tick gridEnd = grid > 0
                ? static_cast<Tick>(std::llround(static_cast<double>(gridEndRaw) / grid)) * grid
                : gridEndRaw;
            const int gridMeasure = findMeasure(measures, gridStart);
            const double gridBeat = gridMeasure >= 0
                ? static_cast<double>(gridStart - measures[gridMeasure].start) / MusicDocument::kPpq + 1.0 : 0.0;
            note.gridStart = {gridStart, gridMeasure, gridBeat};
            note.gridEnd = {gridEnd, findMeasure(measures, gridEnd),
                            gridBeat + static_cast<double>(gridEnd - gridStart) / MusicDocument::kPpq};
        }
    }
}

void MusicAnalyzer::buildChordsAndTies(MusicDocument& document)
{
    quint64 nextChord = 1;
    quint64 nextTie = 1;
    for (auto& track : document.tracks()) {
        track.chords.clear();
        track.holds.clear();
        track.ties.clear();
        track.drumMap.clear();
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
            if (note.duration > MusicDocument::kPpq / 8) track.holds.push_back({note.noteId, note.start,
                                                                 note.start + note.duration, -1});
            if (track.percussion && std::none_of(track.drumMap.cbegin(), track.drumMap.cend(),
                                                 [&](const auto& drum) { return drum.pitch == note.pitch; })) {
                track.drumMap.push_back({note.pitch, QStringLiteral("GM-%1").arg(note.pitch), note.pitch, 0});
            }
        }
        for (int i = 0; i < track.notes.size(); ++i) {
            auto& note = track.notes[i];
            if (!note.tieStart && !note.tieStop) continue;
            if (note.tieStop && i > 0 && track.notes[i - 1].pitch == note.pitch
                && track.notes[i - 1].voice == note.voice && track.notes[i - 1].tieGroupId != 0) {
                note.tieGroupId = track.notes[i - 1].tieGroupId;
            } else {
                note.tieGroupId = nextTie++;
                track.ties.push_back({note.tieGroupId, {}});
            }
            auto tieIt = std::find_if(track.ties.begin(), track.ties.end(), [&](const auto& tie) {
                return tie.id == note.tieGroupId;
            });
            if (tieIt != track.ties.end()) tieIt->noteIds.push_back(note.noteId);
        }
    }
}

QVector<TupletCandidate> MusicAnalyzer::detectTuplets(const MusicDocument& document, Tick grid)
{
    QVector<TupletCandidate> result;
    if (grid <= 0) return result;
    for (const auto& track : document.tracks()) {
        QVector<Tick> starts;
        for (const auto& note : track.notes) starts.push_back(note.start);
        std::sort(starts.begin(), starts.end());
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
