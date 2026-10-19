#pragma once

// 背景图预检与受限尺寸解码，WebP 使用独立库以避免依赖 Qt 插件。
#include "domain/settings/playersettings.h"

#include <QImage>
#include <QString>

namespace midi_play::presentation::visualization {

// 检查格式可读性；WebP 额外限制文件大小并拒绝动画。
bool canReadBackgroundImage(const QString& path, QString* error = nullptr);
// 同步解码为 RGBA8888，长边最多为 maximumDimension；失败返回空图。
QImage loadBackgroundImage(const QString& path,
                           int maximumDimension = midi_play::settings::kDefaultBackgroundImageMaximumDimension);

} // namespace midi_play::presentation::visualization
