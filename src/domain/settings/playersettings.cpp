#include "playersettings.h"

namespace midi_play::settings {

bool isSupportedVisualizationRefreshRate(int refreshRate) noexcept
{
    return refreshRate == 30 || refreshRate == 60 || refreshRate == 120;
}

int normalizeVisualizationRefreshRate(int refreshRate) noexcept
{
    return isSupportedVisualizationRefreshRate(refreshRate)
        ? refreshRate : kDefaultVisualizationRefreshRate;
}

int visualizationRefreshIntervalMs(int refreshRate) noexcept
{
    const int normalizedRefreshRate = normalizeVisualizationRefreshRate(refreshRate);
    return qMax(1, 1000 / normalizedRefreshRate);
}

} // namespace midi_play::settings
