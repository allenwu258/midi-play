#pragma once

#include <QString>

namespace midi_play::audio {

enum class SoundFontFormat {
    Unknown,
    Sf2,
    Sf3
};

struct SoundFontInspection {
    QString path;
    SoundFontFormat format = SoundFontFormat::Unknown;
    bool readable = false;
    bool validRiffContainer = false;
    bool compressedSamples = false;
    int majorVersion = 0;
    int minorVersion = 0;
    qint64 fileSize = 0;
};

class SoundFontInspector final {
public:
    static SoundFontInspection inspect(const QString& path, QString* error = nullptr);
};

} // namespace midi_play::audio
