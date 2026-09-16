#include "apptheme.h"

namespace midi_play::presentation::theme {
namespace {

AppTheme makeTheme(midi_play::settings::ThemeMode mode)
{
    AppTheme theme;
    theme.mode = mode;
    const bool light = mode == midi_play::settings::ThemeMode::Light;
    const auto color = [light](const char* dark, const char* pale) {
        return QColor(QLatin1StringView(light ? pale : dark));
    };
    auto& w = theme.widgets;
    w.window = color("#121416", "#f3f5f2");
    w.panel = color("#1b1d20", "#ffffff");
    w.text = color("#f0f1ed", "#202723");
    w.fileText = color("#c8cbc7", "#39473e");
    w.mutedText = color("#8f9691", "#606b65");
    w.secondaryText = color("#bfc3bf", "#4e5e54");
    w.timeText = color("#e6e7e2", "#28392f");
    w.separator = color("#3a3d40", "#cbd3cd");
    w.buttonText = color("#dfe1dc", "#293b30");
    w.hover = color("#292c2f", "#e5ebe5");
    w.border = color("#3a3e41", "#a6b4aa");
    w.pressed = color("#34383b", "#d2ded4");
    w.disabledText = color("#676c68", "#859188");
    w.actionBorder = color("#4b5350", "#687c6e");
    w.play = color("#176b56", "#176b56");
    w.playHover = color("#21866b", "#21866b");
    w.playEdge = color("#48c9a2", "#177958");
    w.playPressed = color("#0f5141", "#0f5141");
    w.pause = color("#76581d", "#76581d");
    w.pauseHover = color("#967126", "#967126");
    w.pauseEdge = color("#f2c45c", "#926610");
    w.pausePressed = color("#5d4517", "#5d4517");
    w.stop = color("#71323a", "#71323a");
    w.stopHover = color("#91434c", "#91434c");
    w.stopEdge = color("#f07b86", "#a73b4a");
    w.stopPressed = color("#56252c", "#56252c");
    w.disabled = color("#24282a", "#e7ece7");
    w.disabledBorder = color("#363b3b", "#ced7cf");
    w.divider = color("#303337", "#d7ded8");
    w.metronomeText = color("#eaf2ff", "#284b6d");
    w.metronome = color("#314259", "#e2edf7");
    w.metronomeBorder = color("#5a7395", "#849db5");
    w.metronomeHover = color("#405878", "#cddff0");
    w.metronomeEdge = color("#8ab8ef", "#4e7aa2");
    w.checkedText = color("#fff8d5", "#664707");
    w.checked = color("#725e22", "#f4e6b8");
    w.checkedBorder = color("#e7c75d", "#ad882f");
    w.checkedHover = color("#927b2d", "#ecd78f");
    w.closeHover = color("#c42b2b", "#c42b2b");
    w.closePressed = color("#a51f1f", "#a51f1f");
    w.sliderTrack = color("#393d3f", "#ced9d0");
    w.accent = color("#f4d35e", "#987116");
    w.input = color("#25282b", "#eef2ed");
    w.inputHoverBorder = color("#555b5f", "#6d8475");
    w.readOnly = color("#202326", "#f2f5f1");
    w.inputText = color("#d8dbd7", "#34473b");
    w.inputHover = color("#2d3033", "#e2eae2");
    w.hintText = color("#aeb4af", "#5c6b61");
    w.errorText = color("#ffb4a8", "#ae293b");
    w.rateText = color("#e6fff6", "#18513f");
    w.rate = color("#27453d", "#e1eee6");
    w.rateBorder = color("#3c7766", "#7da58d");
    w.rateHover = color("#326153", "#cde4d6");
    w.focus = color("#55c9a4", "#157653");
    w.ratePressed = color("#1d3831", "#bad7c7");
    w.popup = color("#202528", "#f7faf5");
    w.popupBorder = color("#4e625b", "#879c8e");
    w.popupText = color("#e6ebe8", "#314939");
    w.popupInput = color("#2a3033", "#ffffff");
    w.popupInputBorder = color("#4a5652", "#8da696");
    w.rateTrack = color("#3b4542", "#c7d8cc");
    w.rateAccent = color("#35bd94", "#25815e");
    w.handle = color("#f7faf8", "#365e48");
    w.handleHover = color("#a9f5d8", "#197747");
    w.onAccent = QColor("#f7faf8");
    w.iconDisabled = color("#65706b", "#89988d");
    auto& v = theme.visualization;
    v.background = w.window;
    v.keyboardBackground = color("#101214", "#cbd3cc");
    v.primaryText = w.text;
    v.secondaryText = color("#9da29f", "#58695d");
    v.subtleText = color("#737975", "#69786d");
    v.error = color("#ef656b", "#ac293c");
    v.beatLine = light ? QColor(44, 63, 49, 22) : QColor(255, 255, 255, 18);
    v.measureLine = light ? QColor(44, 63, 49, 48) : QColor(255, 255, 255, 48);
    v.pitchBand = light ? QColor(38, 65, 47, 8) : QColor(255, 255, 255, 7);
    v.strikeLine = color("#f4d35e", "#987116");
    v.strikeGlow = v.strikeLine;
    v.strikeGlow.setAlpha(18);
    v.whiteKey = color("#dedfd9", "#fffefa");
    v.whiteKeyBorder = color("#80847f", "#99a79c");
    v.blackKey = color("#24272a", "#2b302d");
    v.blackKeyBorder = color("#090a0b", "#151d18");
    v.keyText = color("#2c2f2e", "#354239");
    v.drumKey = color("#292d30", "#e0e8e0");
    v.drumKeyBorder = light ? QColor(62, 85, 69, 95) : QColor(255, 255, 255, 35);
    v.tremolo = light ? QColor(26, 49, 34, 160) : QColor(255, 255, 255, 110);
    v.loadingVeil = light ? QColor(243, 245, 242, 175) : QColor(10, 11, 12, 118);
    v.emptyVeil = light ? QColor(243, 245, 242, 200) : QColor(10, 11, 12, 148);
    theme.notes = light
        ? NoteMaterialProfile {{0.53, 0.045, 0.125, 0.66, 0.12},
                               {0.40, 0.045, 0.11, 0.88, 0.10},
                               {0.56, 0, 0.085, 0.22, 0.06}, true,
                               {0.64, 0, 0.12, 1, 0}, {0.73, 0, 0.12, 1, 0}}
        : NoteMaterialProfile {{0.69, 0.055, 0.115, 0.43, 0.12},
                               {0.82, 0.055, 0.09, 0.82, 0.12},
                               {0.70, 0, 0.08, 0.15, 0.06}, false};
    return theme;
}

} // namespace

const AppTheme& themeFor(midi_play::settings::ThemeMode mode)
{
    static const AppTheme dark = makeTheme(midi_play::settings::ThemeMode::Dark);
    static const AppTheme light = makeTheme(midi_play::settings::ThemeMode::Light);
    return mode == midi_play::settings::ThemeMode::Light ? light : dark;
}

QPalette widgetPalette(const AppTheme& theme)
{
    const auto& w = theme.widgets;
    QPalette palette;
    for (auto group : {QPalette::Active, QPalette::Inactive, QPalette::Disabled}) {
        const bool disabled = group == QPalette::Disabled;
        const auto text = disabled ? w.disabledText : w.text;
        palette.setColor(group, QPalette::Window, w.panel);
        palette.setColor(group, QPalette::WindowText, text);
        palette.setColor(group, QPalette::Base, w.input);
        palette.setColor(group, QPalette::AlternateBase, w.readOnly);
        palette.setColor(group, QPalette::Text, text);
        palette.setColor(group, QPalette::PlaceholderText, w.hintText);
        palette.setColor(group, QPalette::Button, disabled ? w.disabled : w.input);
        palette.setColor(group, QPalette::ButtonText, text);
        palette.setColor(group, QPalette::Highlight, w.play);
        palette.setColor(group, QPalette::HighlightedText, w.onAccent);
        palette.setColor(group, QPalette::Accent, w.focus);
        palette.setColor(group, QPalette::Light, w.hover);
        palette.setColor(group, QPalette::Midlight, w.input);
        palette.setColor(group, QPalette::Mid, w.border);
        palette.setColor(group, QPalette::Dark, w.inputHoverBorder);
        palette.setColor(group, QPalette::Shadow, w.divider);
        palette.setColor(group, QPalette::BrightText, w.onAccent);
        palette.setColor(group, QPalette::Link, w.focus);
        palette.setColor(group, QPalette::LinkVisited, w.focus);
        palette.setColor(group, QPalette::ToolTipBase, w.popup);
        palette.setColor(group, QPalette::ToolTipText, w.text);
    }
    return palette;
}

} // namespace midi_play::presentation::theme
