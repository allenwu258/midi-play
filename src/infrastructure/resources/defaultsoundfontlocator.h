#pragma once

#include <QString>

namespace midi_play::resources {

class DefaultSoundFontLocator final {
public:
    static QString locate();
    static QString relativePath();
};

} // namespace midi_play::resources
