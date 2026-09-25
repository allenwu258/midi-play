#pragma once

#include <QString>
#include <memory>

namespace midi_play::encoding {

enum class AudioFileFormat { Wav, Mp3 };

class AudioFileEncoder {
public:
    virtual ~AudioFileEncoder() = default;
    virtual bool open(const QString& path, int sampleRate, int bitrateKbps, QString* error) = 0;
    virtual bool write(const float* left, const float* right, int frames, QString* error) = 0;
    virtual bool finish(QString* error) = 0;
};

std::unique_ptr<AudioFileEncoder> createAudioFileEncoder(AudioFileFormat format);

} // namespace midi_play::encoding
