#pragma once

// Immutable input model for the optional video intro-text overlay.

#include <QString>

namespace midi_play::app {

enum class IntroTextAlignment {
    Left,
    Center,
    Right,
};

enum class IntroTextPosition {
    Center,
    LowerLeft,
    Custom,
};

enum class IntroTextExitEffect {
    Fade,
    SlideDown,
};

// Geometry is expressed in 1280x720 reference-canvas pixels and scaled uniformly
// to the final encoded frame. Times are measured on the output-video timeline.
struct IntroTextConfig {
    bool enabled = false;
    QString titleText;
    QString bodyText;
    QString fontFamily = QStringLiteral("Microsoft YaHei");
    int titleFontPixelSize = 48;
    int bodyFontPixelSize = 32;
    // 1280x720 reference-canvas pen width in tenths of a pixel (0..40).
    int outlineWidthTenths = 8;
    IntroTextAlignment alignment = IntroTextAlignment::Center;
    IntroTextPosition position = IntroTextPosition::Center;
    int offsetX = 0; // Relative to canvas center; positive values move right.
    int offsetY = 0; // Relative to canvas center; positive values move down.
    int widthPercent = 80;
    int displayMilliseconds = 5'000;
    int exitMilliseconds = 1'000;
    IntroTextExitEffect exitEffect = IntroTextExitEffect::Fade;
};

} // namespace midi_play::app
