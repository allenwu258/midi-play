#pragma once

#include "domain/settings/notecolormode.h"
#include "domain/settings/thememode.h"

namespace midi_play::presentation::visualization {

struct MaterialLayer {
    double lightness, velocityLightness, chroma, alpha, velocityAlpha;
};

struct NoteMaterialProfile {
    MaterialLayer body, head, tail;
    bool separateKeyColors = false;
    MaterialLayer keyFill {}, keyTop {};
};

// Immutable presentation policy. It contains no geometry, clock or music state.
struct NoteAppearance {
    midi_play::settings::ThemeMode themeMode;
    midi_play::settings::NoteColorMode colorMode;
    NoteMaterialProfile material;
    double densityFalloff = 0.42;
    double minimumBodyOpacity = 0.52;
};

const NoteAppearance& noteAppearanceFor(
    midi_play::settings::ThemeMode themeMode = midi_play::settings::kDefaultThemeMode,
    midi_play::settings::NoteColorMode colorMode = midi_play::settings::kDefaultNoteColorMode);

} // namespace midi_play::presentation::visualization
