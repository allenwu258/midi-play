#include "playersettings.h"

#include <algorithm>

namespace midi_play::settings {

bool isValidVisualizationRefreshRate(int refreshRate) noexcept
{
    return refreshRate >= kMinimumVisualizationRefreshRate
        && refreshRate <= kMaximumVisualizationRefreshRate;
}

int normalizeVisualizationRefreshRate(int refreshRate) noexcept
{
    return isValidVisualizationRefreshRate(refreshRate)
        ? refreshRate : kDefaultVisualizationRefreshRate;
}

bool isValidPlaybackRatePercent(int percent) noexcept
{
    return percent >= kMinimumPlaybackRatePercent && percent <= kMaximumPlaybackRatePercent;
}

int normalizePlaybackRatePercent(int percent) noexcept
{
    return std::clamp(percent, kMinimumPlaybackRatePercent, kMaximumPlaybackRatePercent);
}

std::chrono::nanoseconds visualizationRefreshPeriod(int refreshRate) noexcept
{
    const auto normalizedRefreshRate = normalizeVisualizationRefreshRate(refreshRate);
    return std::chrono::nanoseconds(1'000'000'000) / normalizedRefreshRate;
}

} // namespace midi_play::settings
