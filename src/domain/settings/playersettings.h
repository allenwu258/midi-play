#pragma once

#include <QtGlobal>

namespace midi_play::settings {

inline constexpr int kDefaultVisualizationRefreshRate = 60;
inline constexpr int kMinimumVisualizationRefreshRate = 30;
inline constexpr int kMaximumVisualizationRefreshRate = 120;
inline constexpr int kSettingsSchemaVersion = 1;

struct PlayerSettings {
    int schemaVersion = kSettingsSchemaVersion;
    int visualizationRefreshRate = kDefaultVisualizationRefreshRate;
};

bool isSupportedVisualizationRefreshRate(int refreshRate) noexcept;
int normalizeVisualizationRefreshRate(int refreshRate) noexcept;
int visualizationRefreshIntervalMs(int refreshRate) noexcept;

} // namespace midi_play::settings
