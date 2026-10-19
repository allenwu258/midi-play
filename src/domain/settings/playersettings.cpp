// 数值设置的校验及规范化实现，供 GUI、播放和存储入口共用。
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
    // 先规范化保证分母为正，再按整数纳秒计算刷新周期。
    return std::chrono::nanoseconds(1'000'000'000) / normalizedRefreshRate;
}

int normalizeBackgroundImageMaximumDimension(int dimension) noexcept
{
    return dimension >= kMinimumBackgroundImageMaximumDimension
        && dimension <= kMaximumBackgroundImageMaximumDimension
        ? dimension : kDefaultBackgroundImageMaximumDimension;
}

} // namespace midi_play::settings
