// 实现钢琴/鼓区分配、虚拟琴键延伸和音乐时间到垂直像素的比例。
#include "scenelayoutengine.h"

#include <algorithm>

namespace midi_play::presentation::visualization {
namespace {

// 将负音级规范化后判断七个自然音级。
bool isWhitePitchClass(int pitchClass)
{
    switch ((pitchClass + 12) % 12) {
    case 0:
    case 2:
    case 4:
    case 5:
    case 7:
    case 9:
    case 11:
        return true;
    default:
        return false;
    }
}

// 找到左侧最近的完整白键；虚拟键允许负音高，沿用循环音级规则。
int previousWhitePitch(int pitch)
{
    do { --pitch; } while (!isWhitePitchClass(pitch % 12));
    return pitch;
}

} // namespace

// 在同一音级规则上派生黑键属性。
bool SceneLayoutEngine::isBlackKey(int pitch)
{
    return !isWhitePitchClass(pitch % 12);
}

// 所有布局值为逻辑像素，视口高度决定键盘及简谱条的受限尺寸。
PlaybackSceneGeometry SceneLayoutEngine::layout(const QSizeF& viewport,
                                                const midi_play::visualization::VisualChart* chart,
                                                qint64 lookAheadUs,
                                                bool showNotationStrip,
                                                const SceneLayoutConstraints& constraints) const
{
    PlaybackSceneGeometry geometry;
    geometry.bounds = QRectF(QPointF(0.0, 0.0), viewport);

    // 计算尺寸至少为一；bounds 仍记录输入视口，供调用方裁剪。
    const qreal width = std::max<qreal>(1.0, viewport.width());
    const qreal height = std::max<qreal>(1.0, viewport.height());
    const qreal keyboardHeight = std::clamp(height * 0.17, 88.0, 132.0);
    const qreal bottom = height;
    geometry.keyboardRect = QRectF(0.0, bottom - keyboardHeight, width, keyboardHeight);
    geometry.fallingRect = QRectF(0.0, 0.0, width,
                                  std::max<qreal>(1.0, geometry.keyboardRect.top()));
    const qreal notationHeight = showNotationStrip ? std::clamp(height * 0.045, 24.0, 38.0) : 0.0;
    geometry.strikeLineY = geometry.keyboardRect.top() - notationHeight;
    if (showNotationStrip) {
        geometry.notationStripRect = QRectF(0.0, geometry.strikeLineY, width, notationHeight);
    }
    // lookAheadUs 对应下落区高度，分母与比例下限避免空尺寸和零时长。
    geometry.pixelsPerMicrosecond = std::max<qreal>(0.000001,
        (geometry.strikeLineY - geometry.fallingRect.top()) / std::max<qint64>(1, lookAheadUs));

    const qreal measureGutter = constraints.leftExtension
        ? constraints.leftExtension->width : (width < 720.0 ? 36.0 : 50.0);
    const int drumCount = chart ? chart->drumLanes().size() : 0;
    const bool hasMelodicNotes = chart && std::any_of(chart->tracks().cbegin(), chart->tracks().cend(),
                                                     [](const auto& track) {
                                                         return !track.percussion && !track.noteIndices.isEmpty();
                                                     });
    qreal drumWidth = 0.0;
    qreal gap = 0.0;
    // 混合曲保留右侧鼓区；纯鼓曲使用除小节标签边栏外的全部宽度。
    if (drumCount > 0) {
        if (hasMelodicNotes) {
            drumWidth = std::clamp(width * 0.19, 112.0, 250.0);
            if (width < 700.0) drumWidth = std::min<qreal>(drumWidth, width * 0.25);
            gap = 8.0;
        } else {
            drumWidth = std::max<qreal>(1.0, width - measureGutter);
        }
    }
    const qreal melodicRight = width - drumWidth - gap;
    const bool showPiano = hasMelodicNotes || drumCount == 0;
    const qreal melodicWidth = showPiano ? std::max<qreal>(80.0, melodicRight) : 0.0;
    const auto range = chart ? chart->pitchRange() : midi_play::visualization::PitchRange {};
    const int minimumPitch = std::clamp(range.minimum, 0, 127);
    const int maximumPitch = std::clamp(range.maximum, minimumPitch, 127);
    int whiteCount = 0;
    for (int pitch = minimumPitch; pitch <= maximumPitch; ++pitch) {
        if (!isBlackKey(pitch)) ++whiteCount;
    }
    whiteCount = std::max(1, whiteCount);

    // 先用完整白键覆盖小节标签边栏，再单次修补左边界的半黑键。
    // 真实键盘右边界和击键线时间坐标不变，虚拟键不参与 MIDI 映射。
    int extraWhiteKeys = 0;
    qreal whiteWidth = showPiano ? melodicWidth / whiteCount : 0.0;
    if (showPiano && constraints.leftExtension) {
        extraWhiteKeys = constraints.leftExtension->whiteKeyCount;
    } else if (showPiano) {
        while (extraWhiteKeys < whiteCount + 128) {
            whiteWidth = melodicWidth / qreal(whiteCount + extraWhiteKeys);
            if (extraWhiteKeys * whiteWidth + 0.001 >= measureGutter) break;
            ++extraWhiteKeys;
        }
    }
    int firstExtensionPitch = minimumPitch;
    for (int i = 0; i < extraWhiteKeys; ++i)
        firstExtensionPitch = previousWhitePitch(firstExtensionPitch);
    if (!constraints.leftExtension && extraWhiteKeys > 0 && isBlackKey(firstExtensionPitch - 1)) {
        // 原左边界上的黑键需要左侧白键承托，只修补一次；
        // 新白键外侧若仍有黑键则省略，不能继续递归扩展。
        firstExtensionPitch = previousWhitePitch(firstExtensionPitch);
        ++extraWhiteKeys;
        whiteWidth = melodicWidth / qreal(whiteCount + extraWhiteKeys);
    }
    const qreal extensionWidth = showPiano && constraints.leftExtension
        ? constraints.leftExtension->width : extraWhiteKeys * whiteWidth;
    const qreal pitchWidth = showPiano ? std::max<qreal>(1.0, melodicWidth - extensionWidth) : 0.0;
    geometry.leftKeyboardExtensionRect = QRectF(0.0, geometry.keyboardRect.top(),
                                                extensionWidth, keyboardHeight);
    geometry.pianoRect = showPiano
        ? QRectF(extensionWidth, geometry.keyboardRect.top(), pitchWidth, keyboardHeight)
        : QRectF(measureGutter, geometry.keyboardRect.top(), 0.0, keyboardHeight);
    if (drumCount > 0) {
        geometry.drumRect = QRectF(geometry.pianoRect.right() + gap, geometry.keyboardRect.top(),
                                   std::max<qreal>(1.0, width - geometry.pianoRect.right() - gap), keyboardHeight);
    }

    whiteWidth = showPiano ? geometry.pianoRect.width() / whiteCount : 0.0;
    // 实际音高使用固定 128 槽；白键依次铺排，黑键中心落在相邻白键边界。
    geometry.pitches.resize(128);
    int whiteOrdinal = 0;
    for (int pitch = minimumPitch; pitch <= maximumPitch; ++pitch) {
        auto& slot = geometry.pitches[pitch];
        slot.pitch = pitch;
        slot.blackKey = isBlackKey(pitch);
        slot.valid = true;
        if (!slot.blackKey) {
            const qreal left = geometry.pianoRect.left() + whiteOrdinal * whiteWidth;
            slot.keyRect = QRectF(left, geometry.keyboardRect.top(), whiteWidth, keyboardHeight);
            slot.centerX = left + whiteWidth * 0.5;
            slot.noteWidth = std::max<qreal>(1.0, whiteWidth * 0.46);
            ++whiteOrdinal;
        } else {
            const qreal center = geometry.pianoRect.left() + whiteOrdinal * whiteWidth;
            const qreal blackWidth = std::max<qreal>(2.0, whiteWidth * 0.62);
            slot.keyRect = QRectF(center - blackWidth * 0.5, geometry.keyboardRect.top(),
                                  blackWidth, keyboardHeight * 0.62);
            slot.centerX = center;
            slot.noteWidth = std::max<qreal>(1.0, whiteWidth * 0.42);
        }
    }

    // 从完整白键开始生成连续虚拟键；白键之间的黑键允许显示，
    // 最外侧白键左侧的黑键整体省略，避免产生半个黑键。
    // 黑键只覆盖区内相邻白键，不进入 MIDI 音高数组，也不接受高亮。
    if (extraWhiteKeys > 0 && !geometry.leftKeyboardExtensionRect.isEmpty()) {
        // 锁定扩展区时虚拟键单独等分；普通窗口仍与真实白键等宽。
        const qreal extensionWhiteWidth = constraints.leftExtension
            ? extensionWidth / extraWhiteKeys : whiteWidth;
        int whiteOrdinal = 0;
        for (int virtualPitch = firstExtensionPitch; virtualPitch < minimumPitch; ++virtualPitch) {
            PitchSlotGeometry slot;
            slot.pitch = virtualPitch;
            slot.blackKey = isBlackKey(virtualPitch);
            slot.valid = true;
            const qreal boundary = whiteOrdinal * extensionWhiteWidth;
            if (slot.blackKey) {
                const qreal blackWidth = std::max<qreal>(2.0, extensionWhiteWidth * 0.62);
                slot.keyRect = QRectF(boundary - blackWidth * 0.5, geometry.keyboardRect.top(),
                                      blackWidth, keyboardHeight * 0.62);
                slot.centerX = boundary;
                slot.noteWidth = std::max<qreal>(1.0, extensionWhiteWidth * 0.42);
            } else {
                slot.keyRect = QRectF(boundary, geometry.keyboardRect.top(), extensionWhiteWidth, keyboardHeight);
                slot.centerX = boundary + extensionWhiteWidth * 0.5;
                slot.noteWidth = std::max<qreal>(1.0, extensionWhiteWidth * 0.46);
                ++whiteOrdinal;
            }
            geometry.leftKeyboardExtensionPitches.push_back(slot);
        }
    }

    if (drumCount > 0) {
        // 鼓道等宽分布，布局下标与图表稳定道编号一致。
        const qreal laneWidth = geometry.drumRect.width() / drumCount;
        geometry.drumSlots.reserve(drumCount);
        for (int lane = 0; lane < drumCount; ++lane) {
            const QRectF keyRect(geometry.drumRect.left() + lane * laneWidth,
                                 geometry.keyboardRect.top(), laneWidth, keyboardHeight);
            geometry.drumSlots.push_back({lane, keyRect, keyRect.center().x(),
                                          std::max<qreal>(2.0, laneWidth * 0.76)});
        }
    }
    return geometry;
}

} // namespace midi_play::presentation::visualization
