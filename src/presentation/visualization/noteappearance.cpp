#include "noteappearance.h"

#include <array>

namespace midi_play::presentation::visualization {
namespace {

using midi_play::settings::NoteColorMode;
using midi_play::settings::ThemeMode;

NoteAppearance makeAppearance(ThemeMode themeMode, NoteColorMode colorMode)
{
    NoteAppearance result {themeMode, colorMode, {}};
    const bool light = themeMode == ThemeMode::Light;
    if (colorMode == NoteColorMode::Normal) {
        // Preserve v0.3.3 materials exactly, including its key/glow relationship.
        result.material = light
            ? NoteMaterialProfile {{0.53, 0.045, 0.125, 0.66, 0.12},
                                   {0.40, 0.045, 0.11, 0.88, 0.10},
                                   {0.56, 0, 0.085, 0.22, 0.06}, true,
                                   {0.64, 0, 0.12, 1, 0}, {0.73, 0, 0.12, 1, 0}}
            : NoteMaterialProfile {{0.69, 0.055, 0.115, 0.43, 0.12},
                                   {0.82, 0.055, 0.09, 0.82, 0.12},
                                   {0.70, 0, 0.08, 0.15, 0.06}, false};
    } else {
        result.material = light
            ? NoteMaterialProfile {{0.54, 0.055, 0.185, 0.78, 0.10},
                                   {0.43, 0.035, 0.14, 0.95, 0},
                                   {0.56, 0, 0.10, 0.22, 0.06}, true,
                                   {0.64, 0, 0.16, 1, 0}, {0.71, 0, 0.14, 1, 0}}
            : NoteMaterialProfile {{0.70, 0.055, 0.185, 0.78, 0.10},
                                   {0.82, 0.035, 0.14, 0.95, 0},
                                   {0.70, 0, 0.10, 0.15, 0.06}, true,
                                   {0.72, 0, 0.16, 1, 0}, {0.83, 0, 0.13, 1, 0}};
        result.densityFalloff = 0.38;
        result.minimumBodyOpacity = 0.62;
    }
    return result;
}

} // namespace

const NoteAppearance& noteAppearanceFor(ThemeMode themeMode, NoteColorMode colorMode)
{
    static const std::array<NoteAppearance, 4> appearances {{
        makeAppearance(ThemeMode::Dark, NoteColorMode::Normal),
        makeAppearance(ThemeMode::Dark, NoteColorMode::Vivid),
        makeAppearance(ThemeMode::Light, NoteColorMode::Normal),
        makeAppearance(ThemeMode::Light, NoteColorMode::Vivid)
    }};
    const auto theme = midi_play::settings::normalizeThemeMode(themeMode);
    const auto colors = midi_play::settings::normalizeNoteColorMode(colorMode);
    return appearances[(theme == ThemeMode::Light ? 2 : 0) + (colors == NoteColorMode::Vivid ? 1 : 0)];
}

} // namespace midi_play::presentation::visualization
