#pragma once

#include "musicdocument.h"

#include <QVector>

// 音乐领域的调性、量化、和弦、连音和 lane 派生分析接口。
namespace midi_play::music {

// 对三连音候选区间的启发式识别结果。
struct TupletCandidate {
    Tick start = 0;
    Tick end = 0;
    int count = 3;
    int inSpaceOf = 2;
    double confidence = 0.0;
};

// 音符到可视化/演奏 lane 的分配结果。
struct LaneAssignment {
    quint64 noteId = 0;
    int lane = -1;
};

// 一次音乐分析产生的派生数据汇总。
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

// 为导入文档补充调性、网格、和弦、连音和布局派生信息。
//
// analyze 会原地修改 MusicDocument 的 KeyContext、NoteEvent 派生字段、
// chord/tie/hold 集合和 lane 字段；调用方应在文档仍处于可变导入阶段使用。
class MusicAnalyzer final {
public:
    // 按固定顺序执行全部分析步骤；laneCount 会被限制为至少 1。
    MusicAnalysisReport analyze(MusicDocument& document, int laneCount = 4) const;

private:
    // 优先使用文档显式调号，否则用音高-时值直方图估计调性。
    static KeyContext estimateKey(const MusicDocument& document, double* confidence);
    // 在候选 tick 网格中选择平均起音误差最小的网格。
    static Tick chooseQuantizationGrid(const MusicDocument& document, double* meanError);
    // 补齐记谱音高、音级、小节位置和量化网格位置。
    static void enrichPitchAndGrid(MusicDocument& document, const KeyContext& key, Tick grid);
    // 按起始位置建立和弦、持续音、连音组和打击乐映射。
    static void buildChordsAndTies(MusicDocument& document);
    // 根据连续起音间隔识别三连音候选。
    static QVector<TupletCandidate> detectTuplets(const MusicDocument& document, Tick grid);
    // 按每条轨道的音域把音符映射到有限数量的 lane。
    static QVector<LaneAssignment> assignLanes(MusicDocument& document, int laneCount);
    // 根据相邻起音间隔估计长短拍比例。
    static double estimateSwingRatio(const MusicDocument& document, Tick grid);
};

} // namespace midi_play::music
