#pragma once

// 根据视口、图表音域与预览时长计算统一场景布局。
#include "domain/visualization/visualchart.h"
#include "scenegeometry.h"

#include <QSizeF>
#include <optional>

namespace midi_play::presentation::visualization {

// 导出裁栏时锁定扩展区；真实琴键宽度由剩余区域独立决定。
struct LeftExtensionConstraint {
    qreal width = 0.0;
    int whiteKeyCount = 0; // 纯鼓曲为零。
    bool operator==(const LeftExtensionConstraint&) const = default;
};

struct SceneLayoutConstraints {
    std::optional<LeftExtensionConstraint> leftExtension;
    bool operator==(const SceneLayoutConstraints&) const = default;
};

// 无后端资源的纯几何计算，实时窗口和离屏导出共用。
class SceneLayoutEngine final {
public:
    // 空图表使用默认音域；可选简谱条将击键线抬到键盘上方。
    PlaybackSceneGeometry layout(const QSizeF& viewport,
                                 const midi_play::visualization::VisualChart* chart,
                                 qint64 lookAheadUs, bool showNotationStrip = false,
                                 const SceneLayoutConstraints& constraints = {}) const;

private:
    // 根据十二音级判断黑键，支持虚拟延伸区的负音高。
    static bool isBlackKey(int pitch);
};

} // namespace midi_play::presentation::visualization
