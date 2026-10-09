#pragma once
// 应用语义色表：GUI 控件与可视化各自使用命名颜色角色。

#include "domain/settings/thememode.h"

#include <QColor>
#include <QPalette>

namespace midi_play::presentation::theme {

// 控件语义颜色，主题不承载布局尺寸或音乐数据。
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
    // 播放/暂停/停止分别提供普通、悬停、边框和按下状态。
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
    // 节拍器与勾选状态使用独立色组。
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
    // 输入、只读及提示/错误文本色。
    QColor input;
    QColor inputHoverBorder;
    QColor readOnly;
    QColor inputText;
    QColor inputHover;
    QColor hintText;
    QColor errorText;
    // 播放倍率弹窗、轨道及手柄色组。
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

// 后端共享的场景颜色，不包括由音符语义派生的材质颜色。
struct VisualizationColors {
    QColor background, keyboardBackground;
    QColor primaryText, secondaryText, subtleText, error;
    QColor beatLine, measureLine, pitchBand, strikeLine, strikeGlow;
    QColor whiteKey, whiteKeyBorder, blackKey, blackKeyBorder, keyText;
    QColor drumKey, drumKeyBorder, tremolo, loadingVeil, emptyVeil;
};

// 一份主题的完整只读快照，控件和渲染器通过同一 mode 取得。
struct AppTheme {
    midi_play::settings::ThemeMode mode = midi_play::settings::kDefaultThemeMode;
    WidgetColors widgets;
    VisualizationColors visualization;
};

// 返回静态主题引用，未知模式采用深色，引用在进程生命周期内有效。
const AppTheme& themeFor(midi_play::settings::ThemeMode mode);
// 将语义色映射为 Qt Active/Inactive/Disabled 调色板。
QPalette widgetPalette(const AppTheme& theme);

} // namespace midi_play::presentation::theme
