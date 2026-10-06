#pragma once

#include <QString>

namespace midi_play::encoding {

struct FfmpegProbeResult {
    QString executablePath;
    QString version;
    QString error;
    bool valid = false;
};

// Resolve a configured executable or auto-detect ffmpeg from PATH. A configured
// path is authoritative: an invalid manual selection must not silently fall
// back to another installation.
FfmpegProbeResult probeFfmpeg(const QString& configuredPath = {}, bool usePath = true);

} // namespace midi_play::encoding
