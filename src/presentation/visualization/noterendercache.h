#pragma once

// 按图表、视口尺寸和外观策略缓存音符样式及后端共用的预计算几何。
#include "domain/visualization/visualchart.h"
#include "scenegeometry.h"
#include "notematerial.h"

#include <QBrush>
#include <QSizeF>
#include <QVector>

namespace midi_play::presentation::visualization {

// 去重后的材质与 Qt 渐变画刷，多个音符可引用同一样式。
struct NoteRenderStyle {
    NoteMaterial material;
    QBrush bodyGradientBrush;
    QBrush tailGradientBrush;
};

// 音符的稳定横向几何及绝对时间，垂直位置在每帧由传输时间计算。
struct PreparedNoteRenderData {
    // 索引指向 styles()；横向位置及宽度使用逻辑像素。
    int styleIndex = -1;
    qreal left = 0.0;
    qreal width = 0.0;
    bool hasTail = false; // 可听结束晚于按键释放时刻时生成踏板尾部。
    bool tremolo = false;
    bool validGeometry = false; // 对应音高或鼓道没有有效宽度时不可绘制。
    // 栅格与 GPU 路径共用的时间负载，配色切换不改动这些字段。
    qint64 startUs = 0;
    qint64 keyEndUs = 0;
    qint64 audibleEndUs = 0;
    quint64 instanceId = 0;
    quint32 flags = 0;
};

// 样式按完整音符材质键去重；图表、横向几何与材质分别维护更新路径。
// 配色/主题切换只重算材质，尺寸变化重算几何，图表替换重建全部音符映射。
class NoteRenderCache final {
public:
    // 按图表身份、外观策略和视口尺寸判定本次必须重建的缓存层。
    void prepare(const midi_play::visualization::VisualChartPtr& chart,
                 const PlaybackSceneGeometry& geometry,
                 midi_play::settings::ThemeMode mode = midi_play::settings::kDefaultThemeMode,
                 midi_play::settings::NoteColorMode colors = midi_play::settings::kDefaultNoteColorMode);
    // 丢弃缓存并推进材质版本，累计构建计数保留。
    void clear();

    // 返回所有去重样式及按源音符下标对齐的预计算记录。
    const QVector<NoteRenderStyle>& styles() const { return m_styles; }
    const QVector<PreparedNoteRenderData>& notes() const { return m_notes; }
    // 非法音符下标返回 nullptr。
    const PreparedNoteRenderData* note(int noteIndex) const;
    // 经音符映射取得样式，音符或样式缺失时返回 nullptr。
    const NoteRenderStyle* styleForNote(int noteIndex) const;

    // 返回缓存保持所有权的图表借用指针。
    const midi_play::visualization::VisualChart* chart() const { return m_chart.get(); }
    // 图表/几何累计构建次数供下游判断缓存是否需要更新。
    quint64 chartBuildCount() const { return m_chartBuildCount; }
    quint64 geometryBuildCount() const { return m_geometryBuildCount; }
    // 材质独立版本，允许 GPU 重新打包颜色而保留音符时间和横向布局。
    quint64 materialRevision() const { return m_materialRevision; }
    // 返回当前静态外观策略。
    const NoteAppearance& appearance() const { return *m_appearance; }

private:
    // 建立样式去重表和每个样式的代表音符。
    void rebuildChart(const midi_play::visualization::VisualChartPtr& chart);
    // 按钢琴/鼓道位置和重叠子道重新计算横向几何。
    void rebuildGeometry(const PlaybackSceneGeometry& geometry);
    // 由代表音符重算每个样式，不改变音符样式下标。
    void rebuildMaterials();

    midi_play::visualization::VisualChartPtr m_chart;
    QSizeF m_geometrySize;
    // 两个数组同下标：每个去重样式由一个源音符代表，用于重算配色。
    QVector<NoteRenderStyle> m_styles;
    QVector<int> m_styleRepresentatives;
    QVector<PreparedNoteRenderData> m_notes; // 与 m_chart->notes() 同下标。
    quint64 m_chartBuildCount = 0;
    quint64 m_geometryBuildCount = 0;
    quint64 m_materialRevision = 0;
    // 外观策略拥有静态存储期，地址变化可用于判断主题/配色策略切换。
    const NoteAppearance* m_appearance = &noteAppearanceFor();
};

} // namespace midi_play::presentation::visualization
