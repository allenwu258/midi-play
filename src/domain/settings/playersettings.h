#pragma once

#include "titlebarmode.h"
#include "graphicsmode.h"
#include "thememode.h"
#include "notecolormode.h"

#include <QString>
#include <QtGlobal>

#include <chrono>

namespace midi_play::settings {

inline constexpr int kDefaultVisualizationRefreshRate = 60;
inline constexpr int kDefaultPlaybackRatePercent = 100;
inline constexpr int kMinimumPlaybackRatePercent = 20;
inline constexpr int kMaximumPlaybackRatePercent = 200;
inline constexpr int kMinimumVisualizationRefreshRate = 1;
inline constexpr int kMaximumVisualizationRefreshRate = 1000;
inline constexpr bool kDefaultShowNotationStrip = false;
inline constexpr int kSettingsSchemaVersion = 7;

struct PlayerSettings {
    int schemaVersion = kSettingsSchemaVersion;
    int visualizationRefreshRate = kDefaultVisualizationRefreshRate;
    GraphicsMode graphicsMode = kDefaultGraphicsMode;
    // Runtime metadata: the settings store derives this from the presence of
    // General/graphicsMode. It is deliberately not serialized separately.
    bool graphicsModeConfigured = false;
    bool showNotationStrip = kDefaultShowNotationStrip;
    ThemeMode themeMode = kDefaultThemeMode;
    NoteColorMode noteColorMode = kDefaultNoteColorMode;
    TitleBarMode titleBarMode = kDefaultTitleBarMode;
    // Empty means unconfigured. Only successful user selections are persisted.
    QString soundFontPath;
};

bool isValidVisualizationRefreshRate(int refreshRate) noexcept;
int normalizeVisualizationRefreshRate(int refreshRate) noexcept;
bool isValidPlaybackRatePercent(int percent) noexcept;
int normalizePlaybackRatePercent(int percent) noexcept;
std::chrono::nanoseconds visualizationRefreshPeriod(int refreshRate) noexcept;

} // namespace midi_play::settings
