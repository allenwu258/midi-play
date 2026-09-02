#include "playersettings.h"

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

std::chrono::nanoseconds visualizationRefreshPeriod(int refreshRate) noexcept
{
    const auto normalizedRefreshRate = normalizeVisualizationRefreshRate(refreshRate);
    return std::chrono::nanoseconds(1'000'000'000) / normalizedRefreshRate;
}

} // namespace midi_play::settings
