#include "notecolorpalette.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

namespace midi_play::presentation::visualization {
namespace {

constexpr std::array<std::array<double, 3>, 10> kTrackHueFamilies {{
    {150, 178, 207}, {224, 260, 296}, {56, 78, 98}, {12, 30, 50},
    {278, 306, 334}, {175, 203, 233}, {30, 52, 74}, {122, 145, 169},
    {315, 338, 360}, {252, 278, 306}
}};

// A cyclic path through a track's analogous hues, rather than a full rainbow.
// It is independent of key signature, note order, repeats and transport time.
constexpr std::array<double, 12> kPitchRoles {
    0.12, 0.26, 0.48, 0.70, 0.90, 1.00, 0.86, 0.66, 0.42, 0.20, 0.00, 0.02
};

double percussionRole(int pitch)
{
    switch (pitch) {
    case 35: case 36: return 0.10; // Bass drums.
    case 37: case 38: case 39: case 40: return 0.38; // Snare, side stick, clap.
    case 42: case 44: case 46: return 0.92; // Hi-hats.
    case 41: case 43: case 45: case 47: case 48: case 50: return 0.60; // Toms.
    case 49: case 51: case 52: case 53: case 55: case 57: case 59: return 1.0; // Cymbals.
    case 54: case 56: case 58: case 69: case 70: return 0.80; // Bells and shakers.
    case 60: case 61: case 62: case 63: case 64: case 65: case 66: return 0.52; // Hand drums.
    case 67: case 68: case 76: case 77: return 0.66; // Agogos and wood blocks.
    case 71: case 72: case 73: case 74: case 75: case 78: case 79: return 0.28;
    case 80: case 81: return 0.86; // Triangles.
    default: return 0.50;
    }
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

} // namespace

double noteColorHue(const midi_play::visualization::ColorRgba& color)
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

QColor noteColorFromOklch(double lightness, double chroma, double angle, double alpha)
{
    std::array<double, 3> rgb {};
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

double vividNoteHue(int trackIndex, int pitch, bool percussion)
{
    const auto& family = kTrackHueFamilies[trackIndex < 0 ? 5 : size_t(trackIndex) % kTrackHueFamilies.size()];
    const double role = percussion ? percussionRole(pitch) : kPitchRoles[std::clamp(pitch, 0, 127) % 12];
    const int segment = role <= 0.5 ? 0 : 1;
    const double fraction = role <= 0.5 ? role * 2 : (role - 0.5) * 2;
    const double arc = std::remainder(family[segment + 1] - family[segment], 360.0);
    return (family[segment] + arc * fraction) * std::numbers::pi / 180.0;
}

} // namespace midi_play::presentation::visualization
