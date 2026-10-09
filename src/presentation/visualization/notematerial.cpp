#include "notematerial.h"
// 在材质构建阶段处理演奏力度、声部色偏和主题色，避免逐帧重复色彩转换。
#include "notecolorpalette.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace midi_play::presentation::visualization {
namespace {

using midi_play::visualization::VisualNote;

// 重音/强重音提高有效力度，再按八级粒度分桶以压缩样式数量。
int velocityBucket(const VisualNote& note)
{
    qint64 velocity = note.velocity;
    if (note.flags & midi_play::visualization::AccentNote) velocity += 18;
    if (note.flags & midi_play::visualization::MarcatoNote) velocity += 24;
    return int(std::clamp<qint64>(velocity, 1, 127) / 8);
}

// 谱表和声部只提供有限色相偏移，不推断演奏者左右手。
int voiceTint(const VisualNote& note)
{
    // 默认 MIDI 谱表/声部保持中性，偏移上限限制为七级。
    return int(std::clamp<qint64>((qint64(note.staff) - 1) * 3 + note.voice - 1, 0, 7));
}

} // namespace

// 两种配色使用相同键，保证模式切换只影响材质而非音符索引。
NoteStyleKey noteMaterialKey(const VisualNote& note)
{
    return {note.trackIndex, std::clamp(note.pitch, 0, 127), voiceTint(note),
            velocityBucket(note), note.isGhost(), note.isPercussion()};
}

// 生成未预乘颜色；力度桶中心、音区和 ghost 属性控制亮度、透明度及能量。
NoteMaterial makeNoteMaterial(const midi_play::visualization::VisualChart& chart,
                              const VisualNote& note, const NoteAppearance& appearance)
{
    const auto key = noteMaterialKey(note);
    const bool validTrack = note.trackIndex >= 0 && note.trackIndex < chart.tracks().size();
    const auto base = validTrack
        ? chart.tracks()[note.trackIndex].color
        : midi_play::visualization::ColorRgba {65, 199, 207, 255};
    const double velocity = (key.velocityBucket * 8.0 + 4.0) / 127.0;
    const bool vivid = appearance.colorMode == midi_play::settings::NoteColorMode::Vivid;
    const double registerOffset = std::clamp((key.pitch / 6 - 10) * 3.0, -21.0, 21.0);
    const double angle = vivid
        ? vividNoteHue(validTrack ? key.trackIndex : -1, key.pitch, key.percussion)
            + key.voiceTint * 0.7 * std::numbers::pi / 180.0
        : noteColorHue(base) + (registerOffset + key.voiceTint * 2.0) * std::numbers::pi / 180.0;
    const double registerLightness = vivid && !key.percussion
        ? std::clamp((key.pitch / 12 - 5) * 0.01, -0.025, 0.025) : 0;
    const double ghost = key.ghost ? 0.48 : 1.0;
    const auto& profile = appearance.material;
    // 所有材质层共用稳定色相，按外观参数分别计算亮度和 alpha。
    const auto layer = [&](const MaterialLayer& values, double lightnessOffset = 0.0) {
        return noteColorFromOklch(values.lightness + velocity * values.velocityLightness + lightnessOffset,
                               values.chroma, angle,
                               (values.alpha + velocity * values.velocityAlpha) * ghost);
    };
    NoteMaterial result;
    result.body = layer(profile.body, registerLightness);
    result.head = layer(profile.head);
    result.tail = layer(profile.tail);
    result.keyFill = profile.separateKeyColors ? layer(profile.keyFill) : result.body;
    result.keyTop = profile.separateKeyColors ? layer(profile.keyTop) : result.head;
    result.glow = profile.separateKeyColors ? result.body : result.head;
    result.energy = float(std::pow(velocity, 0.7) * ghost);
    return result;
}

} // namespace midi_play::presentation::visualization
