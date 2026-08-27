#pragma once

#include "musicdocument.h"

#include <QString>
#include <memory>

namespace midi_play::music {

struct ReadResult {
    std::shared_ptr<MusicDocument> document;
    QString error;

    bool ok() const { return document && error.isEmpty(); }
};

} // namespace midi_play::music
