#pragma once

#include "domain/music/musicdocument.h"
#include "infrastructure/encoding/audiofileencoder.h"

#include <QString>
#include <atomic>
#include <functional>
#include <memory>

namespace midi_play::app {

struct AudioExportOptions {
    QString soundFontPath;
    QString outputPath;
    encoding::AudioFileFormat format = encoding::AudioFileFormat::Mp3;
    int sampleRate = 44100;
    int bitrateKbps = 192;
    bool includeMetronome = false;
    int tailMilliseconds = 500;
};

enum class AudioExportStatus { Success, Failed, Canceled };

struct AudioExportResult {
    AudioExportStatus status = AudioExportStatus::Failed;
    QString error;
    qint64 frames = 0;
    float peakLeft = 0.0f;
    float peakRight = 0.0f;
    quint64 clippedSamples = 0;
};

class AudioExportService final {
public:
    using Progress = std::function<void(int percent)>;

    static AudioExportResult exportDocument(
        std::shared_ptr<const music::MusicDocument> document,
        const AudioExportOptions& options,
        const std::atomic_bool* canceled = nullptr,
        Progress progress = {});
};

} // namespace midi_play::app
