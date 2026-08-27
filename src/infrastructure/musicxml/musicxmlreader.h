#pragma once

#include "domain/music/musicdocument.h"

#include <QString>

namespace midi_play::musicxml {

struct ReadResult {
    std::shared_ptr<music::MusicDocument> document;
    QString error;
    bool ok() const { return document && error.isEmpty(); }
};

class MusicXmlReader final {
public:
    ReadResult read(const QString& path) const;
};

} // namespace midi_play::musicxml
