#pragma once

#include "apptheme.h"
#include <QIcon>

namespace midi_play::presentation::theme {

enum class IconGlyph { Minimize, Maximize, Restore, Close, Play, Pause, Stop, Open, Export, Settings };
QIcon themedIcon(IconGlyph glyph, const AppTheme& theme);

} // namespace midi_play::presentation::theme
