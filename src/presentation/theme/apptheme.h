#pragma once

#include "domain/settings/thememode.h"

#include <QColor>
#include <QPalette>

namespace midi_play::presentation::theme {

// Semantic color roles; dimensions and musical data do not belong to a theme.
struct WidgetColors {
    QColor window;
    QColor panel;
    QColor text;
    QColor fileText;
    QColor mutedText;
    QColor secondaryText;
    QColor timeText;
    QColor separator;
    QColor buttonText;
    QColor hover;
    QColor border;
    QColor pressed;
    QColor disabledText;
    QColor actionBorder;
    QColor play;
    QColor playHover;
    QColor playEdge;
    QColor playPressed;
    QColor pause;
    QColor pauseHover;
    QColor pauseEdge;
    QColor pausePressed;
    QColor stop;
    QColor stopHover;
    QColor stopEdge;
    QColor stopPressed;
    QColor disabled;
    QColor disabledBorder;
    QColor divider;
    QColor metronomeText;
    QColor metronome;
    QColor metronomeBorder;
    QColor metronomeHover;
    QColor metronomeEdge;
    QColor checkedText;
    QColor checked;
    QColor checkedBorder;
    QColor checkedHover;
    QColor closeHover;
    QColor closePressed;
    QColor sliderTrack;
    QColor accent;
    QColor input;
    QColor inputHoverBorder;
    QColor readOnly;
    QColor inputText;
    QColor inputHover;
    QColor hintText;
    QColor errorText;
    QColor rateText;
    QColor rate;
    QColor rateBorder;
    QColor rateHover;
    QColor focus;
    QColor ratePressed;
    QColor popup;
    QColor popupBorder;
    QColor popupText;
    QColor popupInput;
    QColor popupInputBorder;
    QColor rateTrack;
    QColor rateAccent;
    QColor handle;
    QColor handleHover;
    QColor onAccent;
    QColor iconDisabled;
};

struct VisualizationColors {
    QColor background, keyboardBackground;
    QColor primaryText, secondaryText, subtleText, error;
    QColor beatLine, measureLine, pitchBand, strikeLine, strikeGlow;
    QColor whiteKey, whiteKeyBorder, blackKey, blackKeyBorder, keyText;
    QColor drumKey, drumKeyBorder, tremolo, loadingVeil, emptyVeil;
};

struct MaterialLayer {
    double lightness, velocityLightness, chroma, alpha, velocityAlpha;
};

struct NoteMaterialProfile {
    MaterialLayer body, head, tail;
    // Dark uses the note body/head for keys and the head for glows. Light
    // separates key highlights from its darker attack edge and uses body glows.
    bool separateKeyColors = false;
    MaterialLayer keyFill {}, keyTop {};
};

struct AppTheme {
    midi_play::settings::ThemeMode mode = midi_play::settings::kDefaultThemeMode;
    WidgetColors widgets;
    VisualizationColors visualization;
    NoteMaterialProfile notes;
};

const AppTheme& themeFor(midi_play::settings::ThemeMode mode);
QPalette widgetPalette(const AppTheme& theme);

} // namespace midi_play::presentation::theme
