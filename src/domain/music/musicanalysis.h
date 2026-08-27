#pragma once

#include "musicdocument.h"

#include <QVector>

namespace midi_play::music {

struct TupletCandidate {
    Tick start = 0;
    Tick end = 0;
    int count = 3;
    int inSpaceOf = 2;
    double confidence = 0.0;
};

struct LaneAssignment {
    quint64 noteId = 0;
    int lane = -1;
};

struct MusicAnalysisReport {
    KeyContext estimatedKey;
    double keyConfidence = 0.0;
    Tick quantizationGrid = 0;
    double meanQuantizationError = 0.0;
    double swingRatio = 1.0;
    bool humanPerformance = false;
    QVector<TupletCandidate> tuplets;
    QVector<LaneAssignment> lanes;
};

class MusicAnalyzer final {
public:
    MusicAnalysisReport analyze(MusicDocument& document, int laneCount = 4) const;

private:
    static KeyContext estimateKey(const MusicDocument& document, double* confidence);
    static Tick chooseQuantizationGrid(const MusicDocument& document, double* meanError);
    static void enrichPitchAndGrid(MusicDocument& document, const KeyContext& key, Tick grid);
    static void buildChordsAndTies(MusicDocument& document);
    static QVector<TupletCandidate> detectTuplets(const MusicDocument& document, Tick grid);
    static QVector<LaneAssignment> assignLanes(MusicDocument& document, int laneCount);
    static double estimateSwingRatio(const MusicDocument& document, Tick grid);
};

} // namespace midi_play::music
