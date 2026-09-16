#pragma once

#include "apptheme.h"
#include <QString>

namespace midi_play::presentation::theme {

QString mainWindowStyle(const AppTheme& theme);
QString settingsDialogStyle(const AppTheme& theme);
QString playbackRateStyle(const AppTheme& theme);

} // namespace midi_play::presentation::theme
