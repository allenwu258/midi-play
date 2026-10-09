#pragma once

// 单次绘制的共享输入：传输、可见窗口和外观偏好。
#include "domain/playback/playbacktypes.h"
#include "domain/settings/thememode.h"
#include "domain/settings/notecolormode.h"
#include "domain/settings/visualeffectlevel.h"
#include "domain/settings/backgroundimagealignment.h"
#include "visualchart.h"

#include <span>

namespace midi_play::visualization {

// 渲染状态快照；图表只读共享，候选下标借用可见窗口缓存。
struct PlaybackSceneState {
    // 以传输位置为基准，向过去保留余辉、向未来扩展预览时间。
    void updateVisibleWindow()
    {
        visibleWindowStartUs = transportPositionUs - afterglowUs;
        visibleWindowEndUs = transportPositionUs + lookAheadUs;
    }

    VisualChartPtr chart;
    playback::State transportState = playback::State::Empty;
    VisualTime transportPositionUs = 0;
    VisualTime durationUs = 0;
    VisualTime lookAheadUs = 5'000'000;
    VisualTime afterglowUs = 240'000;
    VisualTime effectsStartUs = 0; // seek/恢复时限制旧击键效果的起算边界。
    VisualTime visibilityGuardUs = 250'000;
    VisualTime visibleWindowStartUs = -240'000;
    VisualTime visibleWindowEndUs = 5'000'000;
    // 候选可含保护区音符；后端仍须精确筛选，缓存更新后不得沿用旧 span。
    std::span<const int> candidateNoteIndices;
    bool loading = false;
    bool showNotationStrip = false;
    bool visualEffectsEnabled = true;
    settings::VisualEffectLevel visualEffectsLevel = settings::kDefaultVisualEffectLevel;
    settings::ThemeMode themeMode = settings::kDefaultThemeMode;
    settings::NoteColorMode noteColorMode = settings::kDefaultNoteColorMode;
    settings::BackgroundImageAlignment backgroundImageAlignment =
        settings::kDefaultBackgroundImageAlignment;
    int backgroundImageOpacity = settings::kDefaultBackgroundImageOpacity;
    QString errorMessage;
};

} // namespace midi_play::visualization
