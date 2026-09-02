#pragma once

#include "titlebarmode.h"

#include <QtGlobal>

#include <chrono>

namespace midi_play::settings {

inline constexpr int kDefaultVisualizationRefreshRate = 60;
inline constexpr int kMinimumVisualizationRefreshRate = 1;
inline constexpr int kMaximumVisualizationRefreshRate = 1000;
inline constexpr int kSettingsSchemaVersion = 2;

struct PlayerSettings {
    int schemaVersion = kSettingsSchemaVersion;
    int visualizationRefreshRate = kDefaultVisualizationRefreshRate;
    TitleBarMode titleBarMode = kDefaultTitleBarMode;
};

bool isValidVisualizationRefreshRate(int refreshRate) noexcept;
int normalizeVisualizationRefreshRate(int refreshRate) noexcept;
std::chrono::nanoseconds visualizationRefreshPeriod(int refreshRate) noexcept;

} // namespace midi_play::settings
