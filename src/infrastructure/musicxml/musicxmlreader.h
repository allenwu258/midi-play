#pragma once

#include "domain/music/musicdocument.h"
#include "domain/music/musicreadresult.h"

#include <QString>

namespace midi_play::musicxml {

using ReadResult = music::ReadResult;

class MusicXmlReader final {
public:
    ReadResult read(const QString& path) const;
};

} // namespace midi_play::musicxml
