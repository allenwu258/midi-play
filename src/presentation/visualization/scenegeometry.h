#pragma once

// 后端共用的逻辑像素几何：下落区、击键线、钢琴和鼓道。
#include <QRectF>
#include <QVector>

namespace midi_play::presentation::visualization {

// 一个旋律音高的琴键与音符横向位置，valid 区分音域外槽位。
struct PitchSlotGeometry {
    int pitch = -1;
    qreal centerX = 0.0; // 音符中心横坐标，单位为逻辑像素。
    qreal noteWidth = 0.0; // 音符主体宽度，不包含材质光晕外扩。
    QRectF keyRect; // 琴键矩形；黑键和白键分别参与对应层的绘制。
    bool blackKey = false;
    bool valid = false;
};

// 一个连续鼓道的按键矩形与音符宽度。
struct DrumSlotGeometry {
    int lane = -1;
    QRectF keyRect;
    qreal centerX = 0.0;
    qreal noteWidth = 0.0;
};

// 一次布局的完整几何，DPI 换算在具体后端输出时执行。
struct PlaybackSceneGeometry {
    // 矩形均使用同一逻辑像素坐标系；后端在输出时统一应用 DPR。
    QRectF bounds;
    QRectF fallingRect;
    QRectF keyboardRect;
    QRectF pianoRect;
    QRectF leftKeyboardExtensionRect;
    QRectF drumRect;
    QRectF notationStripRect; // 简谱关闭、击键线贴齐键盘时为空。
    qreal strikeLineY = 0.0; // 音符起点抵达当前播放时刻时的纵坐标。
    qreal pixelsPerMicrosecond = 0.0; // 音乐时间差到纵向距离的比例，不包含播放倍率。
    QVector<PitchSlotGeometry> pitches; // 以 MIDI 音高作下标，valid 标识当前音域内的槽位。
    QVector<PitchSlotGeometry> leftKeyboardExtensionPitches;
    QVector<DrumSlotGeometry> drumSlots;

    // 装饰击键线也覆盖不参与 MIDI 映射的左侧白键延伸区。
    qreal strikeLineLeft() const
    {
        return leftKeyboardExtensionRect.isEmpty()
            ? pianoRect.left() : leftKeyboardExtensionRect.left();
    }

    // 仅返回音域内有效槽位，无效/越界音高返回 nullptr。
    const PitchSlotGeometry* pitchSlot(int pitch) const
    {
        return pitch >= 0 && pitch < pitches.size() && pitches[pitch].valid ? &pitches[pitch] : nullptr;
    }

    // 鼓道不复用音高数组，越界道号返回 nullptr。
    const DrumSlotGeometry* drumSlot(int lane) const
    {
        return lane >= 0 && lane < drumSlots.size() ? &drumSlots[lane] : nullptr;
    }
};

} // namespace midi_play::presentation::visualization
