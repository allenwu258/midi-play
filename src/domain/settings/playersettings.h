#pragma once
// 用户设置快照与数值边界，不依赖具体存储格式或 GUI 控件。

#include "titlebarmode.h"
#include "graphicsmode.h"
#include "thememode.h"
#include "notecolormode.h"
#include "visualeffectlevel.h"
#include "backgroundimagealignment.h"
#include "backgroundmode.h"

#include <QString>
#include <QtGlobal>

#include <chrono>

namespace midi_play::settings {

// 刷新率以 Hz 表示，播放倍率以百分比表示；配置 schema 独立于产品版本。
inline constexpr int kDefaultVisualizationRefreshRate = 60;
inline constexpr int kDefaultPlaybackRatePercent = 100;
inline constexpr int kMinimumPlaybackRatePercent = 20;
inline constexpr int kMaximumPlaybackRatePercent = 200;
inline constexpr int kMinimumVisualizationRefreshRate = 1;
inline constexpr int kMaximumVisualizationRefreshRate = 1000;
inline constexpr bool kDefaultShowNotationStrip = false;
inline constexpr int kDefaultBackgroundImageMaximumDimension = 2048;
inline constexpr int kMinimumBackgroundImageMaximumDimension = 256;
inline constexpr int kMaximumBackgroundImageMaximumDimension = 4096;
inline constexpr int kSettingsSchemaVersion = 15;

// 存储层提供原始快照，应用服务负责再次规范化并向消费者发布变更。
struct PlayerSettings {
    int schemaVersion = kSettingsSchemaVersion;
    int visualizationRefreshRate = kDefaultVisualizationRefreshRate;
    GraphicsMode graphicsMode = kDefaultGraphicsMode;
    // 运行时元信息：由 General/graphicsMode 是否存在推导，不单独序列化。
    bool graphicsModeConfigured = false;
    bool showNotationStrip = kDefaultShowNotationStrip;
    ThemeMode themeMode = kDefaultThemeMode;
    NoteColorMode noteColorMode = kDefaultNoteColorMode;
    TitleBarMode titleBarMode = kDefaultTitleBarMode;
    // 空串表示尚未配置；GUI 仅在音源成功加载后持久化用户选择。
    QString soundFontPath;
    bool ffmpegUsePath = true;
    // 手动安装目录，切换为 PATH 检测时仍保留。
    QString ffmpegPath;
    bool backgroundImageEnabled = false;
    BackgroundMode backgroundMode = BackgroundMode::None;
    QString backgroundVideoPath;
    bool backgroundVideoMuted = true;
    // 关闭图片背景时保留路径，供再次启用时恢复。
    QString backgroundImagePath;
    BackgroundImageAlignment backgroundImageAlignment = kDefaultBackgroundImageAlignment;
    int backgroundImageOpacity = kDefaultBackgroundImageOpacity;
    int backgroundImageMaximumDimension = kDefaultBackgroundImageMaximumDimension;
    // 增强流光效果由 Vulkan 提供，应用服务按构建能力及后端限制开关。
    bool visualEffectsEnabled = true;
    VisualEffectLevel visualEffectsLevel = kDefaultVisualEffectLevel;
};

// 检查刷新率是否位于 1..1000 Hz。
bool isValidVisualizationRefreshRate(int refreshRate) noexcept;
// 无效刷新率回退 60 Hz，而非夹取到最近边界。
int normalizeVisualizationRefreshRate(int refreshRate) noexcept;
// 检查播放倍率是否位于 20%..200%。
bool isValidPlaybackRatePercent(int percent) noexcept;
// 播放倍率夹取到支持范围。
int normalizePlaybackRatePercent(int percent) noexcept;
// 将规范化刷新率转换为整数纳秒周期，供精确 GUI 计时器使用。
std::chrono::nanoseconds visualizationRefreshPeriod(int refreshRate) noexcept;
// 背景图最大边长限制在 256..4096，非法值回退到默认 2048。
int normalizeBackgroundImageMaximumDimension(int dimension) noexcept;

} // namespace midi_play::settings
