#pragma once

// 将音乐事实投影为与播放调度共用时间基准的只读图表。
#include "domain/music/musicdocument.h"
#include "visualchart.h"

#include <memory>

namespace midi_play::visualization {

// 音域保护、延音连线合并和标题回退策略。
struct VisualizationProjectionOptions {
    int minimumPitchSpan = 24;
    int pitchGuardSemitones = 2;
    bool mergeTies = true;
    QString fallbackTitle;
};

// 构建反复实例、踏板尾音、简谱语义和有序元数据，不参与绘制。
class PlaybackVisualizationProjector final {
public:
    // 从已定稿文档生成图表；无效文档返回空指针并填写可选错误。
    VisualChartPtr project(const music::MusicDocument& document,
                           quint64 generation,
                           const VisualizationProjectionOptions& options = {},
                           QString* error = nullptr) const;
};

} // namespace midi_play::visualization
