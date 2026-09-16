#include "notematerial.h"
#include "notecolorpalette.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace midi_play::presentation::visualization {
namespace {

using midi_play::visualization::VisualNote;

int velocityBucket(const VisualNote& note)
{
    qint64 velocity = note.velocity;
    if (note.flags & midi_play::visualization::AccentNote) velocity += 18;
    if (note.flags & midi_play::visualization::MarcatoNote) velocity += 24;
    return int(std::clamp<qint64>(velocity, 1, 127) / 8);
}

int voiceTint(const VisualNote& note)
{
    // The default MIDI voice/staff remains neutral. This is a color variation,
    // not an inference about the performer's left or right hand.
    return int(std::clamp<qint64>((qint64(note.staff) - 1) * 3 + note.voice - 1, 0, 7));
}

} // namespace

NoteStyleKey noteMaterialKey(const VisualNote& note)
{
    return {note.trackIndex, std::clamp(note.pitch, 0, 127), voiceTint(note),
            velocityBucket(note), note.isGhost(), note.isPercussion()};
}

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
