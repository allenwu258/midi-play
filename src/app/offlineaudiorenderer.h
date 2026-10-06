#pragma once
#include "audioexportservice.h"

namespace midi_play::app {
struct OfflineAudioOptions {
    QString soundFontPath;
    int sampleRate = 48000;
    int ratePercent = 100;
    bool includeMetronome = false;
    qint64 totalFrames = 0;
};
class OfflineAudioRenderer final {
public:
    using Sink = std::function<bool(const float*, const float*, int, QString*)>;
    static AudioExportResult render(std::shared_ptr<const music::MusicDocument> document,
        const OfflineAudioOptions& options, Sink sink, const std::atomic_bool* canceled = nullptr,
        AudioExportService::Progress progress = {});
};
} // namespace midi_play::app
