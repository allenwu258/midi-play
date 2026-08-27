#pragma once

#include "domain/music/musicreadresult.h"

#include <QString>

namespace midi_play::readers {

class IMusicReader {
public:
    virtual ~IMusicReader() = default;
    virtual bool canRead(const QString& suffix) const = 0;
    virtual music::ReadResult read(const QString& path) const = 0;
};

} // namespace midi_play::readers
