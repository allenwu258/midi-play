#pragma once

#include "titlebarmode.h"
#include "graphicsmode.h"

#include <QString>
#include <QtGlobal>

#include <chrono>

namespace midi_play::settings {

inline constexpr int kDefaultVisualizationRefreshRate = 60;
inline constexpr int kMinimumVisualizationRefreshRate = 1;
inline constexpr int kMaximumVisualizationRefreshRate = 1000;
inline constexpr int kSettingsSchemaVersion = 4;

struct PlayerSettings {
    int schemaVersion = kSettingsSchemaVersion;
    int visualizationRefreshRate = kDefaultVisualizationRefreshRate;
    GraphicsMode graphicsMode = kDefaultGraphicsMode;
    TitleBarMode titleBarMode = kDefaultTitleBarMode;
    // Empty means that the bundled default SoundFont follows the application.
    QString soundFontPathOverride;
};

bool isValidVisualizationRefreshRate(int refreshRate) noexcept;
int normalizeVisualizationRefreshRate(int refreshRate) noexcept;
std::chrono::nanoseconds visualizationRefreshPeriod(int refreshRate) noexcept;

} // namespace midi_play::settings
