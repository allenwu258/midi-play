#pragma once

// 音符配色与透明度策略，与几何、时钟和播放状态解耦。
#include "domain/settings/notecolormode.h"
#include "domain/settings/thememode.h"

namespace midi_play::presentation::visualization {

// OKLCH 材质层参数，力度可分别影响亮度和透明度。
struct MaterialLayer {
    double lightness, velocityLightness, chroma, alpha, velocityAlpha;
};

// 音符体/头/尾及可选独立键盘高亮的材质参数组。
struct NoteMaterialProfile {
    MaterialLayer body, head, tail;
    bool separateKeyColors = false;
    MaterialLayer keyFill {}, keyTop {};
};

// 不可变外观策略，包含主题、配色、材质与密度衰减参数。
struct NoteAppearance {
    midi_play::settings::ThemeMode themeMode;
    midi_play::settings::NoteColorMode colorMode;
    NoteMaterialProfile material;
    double densityFalloff = 0.42;
    double minimumBodyOpacity = 0.52;
};

// 返回规范化主题/配色对应的静态策略，引用在进程生命周期内有效。
const NoteAppearance& noteAppearanceFor(
    midi_play::settings::ThemeMode themeMode = midi_play::settings::kDefaultThemeMode,
    midi_play::settings::NoteColorMode colorMode = midi_play::settings::kDefaultNoteColorMode);

} // namespace midi_play::presentation::visualization
