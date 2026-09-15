#include "notematerial.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

namespace midi_play::presentation::visualization {
namespace {

using midi_play::visualization::VisualNote;

int velocityBucket(const VisualNote& note)
{
    int velocity = note.velocity;
    if (note.flags & midi_play::visualization::AccentNote) velocity += 18;
    if (note.flags & midi_play::visualization::MarcatoNote) velocity += 24;
    return std::clamp(velocity, 1, 127) / 8;
}

int voiceTint(const VisualNote& note)
{
    // The default MIDI voice/staff remains neutral. This is a color variation,
    // not an inference about the performer's left or right hand.
    return std::clamp((note.staff - 1) * 3 + note.voice - 1, 0, 7);
}

double linear(double value)
{
    return value <= 0.04045 ? value / 12.92 : std::pow((value + 0.055) / 1.055, 2.4);
}

double srgb(double value)
{
    value = std::clamp(value, 0.0, 1.0);
    return value <= 0.0031308 ? value * 12.92 : 1.055 * std::pow(value, 1.0 / 2.4) - 0.055;
}

double hue(const midi_play::visualization::ColorRgba& color)
{
    const double r = linear(color.red / 255.0);
    const double g = linear(color.green / 255.0);
    const double b = linear(color.blue / 255.0);
    const double l = std::cbrt(0.4122214708*r + 0.5363325363*g + 0.0514459929*b);
    const double m = std::cbrt(0.2119034982*r + 0.6806995451*g + 0.1073969566*b);
    const double s = std::cbrt(0.0883024619*r + 0.2817188376*g + 0.6299787005*b);
    return std::atan2(0.0259040371*l + 0.7827717662*m - 0.8086757660*s,
                      1.9779984951*l - 2.4285922050*m + 0.4505937099*s);
}

QColor perceptualColor(double lightness, double chroma, double angle, double alpha)
{
    std::array<double, 3> rgb {};
    // Reduce chroma into sRGB instead of clipping individual color channels.
    for (int attempt = 0; attempt < 24; ++attempt) {
        const double a = chroma * std::cos(angle);
        const double b = chroma * std::sin(angle);
        const double l = std::pow(lightness + 0.3963377774*a + 0.2158037573*b, 3);
        const double m = std::pow(lightness - 0.1055613458*a - 0.0638541728*b, 3);
        const double s = std::pow(lightness - 0.0894841775*a - 1.2914855480*b, 3);
        rgb = {4.0767416621*l - 3.3077115913*m + 0.2309699292*s,
               -1.2684380046*l + 2.6097574011*m - 0.3413193965*s,
               -0.0041960863*l - 0.7034186147*m + 1.7076147010*s};
        if (std::all_of(rgb.begin(), rgb.end(), [](double v) { return v >= 0 && v <= 1; })) break;
        chroma *= 0.90;
    }
    return QColor::fromRgbF(srgb(rgb[0]), srgb(rgb[1]), srgb(rgb[2]), alpha);
}

} // namespace

quint64 noteMaterialKey(const VisualNote& note)
{
    return (quint64(std::max(0, note.trackIndex)) << 24)
        | (quint64(std::clamp(note.pitch, 0, 127) / 6) << 12)
        | (quint64(voiceTint(note)) << 8)
        | (quint64(velocityBucket(note)) << 1) | quint64(note.isGhost());
}

NoteMaterial makeNoteMaterial(const midi_play::visualization::VisualChart& chart,
                              const VisualNote& note)
{
    const auto base = note.trackIndex >= 0 && note.trackIndex < chart.tracks().size()
        ? chart.tracks()[note.trackIndex].color
        : midi_play::visualization::ColorRgba {65, 199, 207, 255};
    const double velocity = (velocityBucket(note) * 8.0 + 4.0) / 127.0;
    const double registerOffset = std::clamp((note.pitch / 6 - 10) * 3.0, -21.0, 21.0);
    const double angle = hue(base) + (registerOffset + voiceTint(note) * 2.0)
        * std::numbers::pi / 180.0;
    const double ghost = note.isGhost() ? 0.48 : 1.0;
    NoteMaterial result;
    result.body = perceptualColor(0.69 + velocity * 0.055, 0.115, angle,
                                 (0.43 + velocity * 0.12) * ghost);
    result.head = perceptualColor(0.82 + velocity * 0.055, 0.09, angle,
                                 (0.82 + velocity * 0.12) * ghost);
    result.tail = perceptualColor(0.70, 0.08, angle, (0.15 + velocity * 0.06) * ghost);
    result.energy = float(std::pow(velocity, 0.7) * ghost);
    return result;
}

} // namespace midi_play::presentation::visualization
