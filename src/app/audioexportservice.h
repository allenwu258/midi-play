#pragma once
// 独立音频文件导出接口，编排参数检查、离线合成及 WAV/MP3 编码。

#include "domain/music/musicdocument.h"
#include "infrastructure/encoding/audiofileencoder.h"

#include <QString>
#include <atomic>
#include <functional>
#include <memory>

namespace midi_play::app {

// 音频导出使用原速；尾音单位为毫秒，码率仅影响 MP3。
struct AudioExportOptions {
    QString soundFontPath;
    QString outputPath;
    encoding::AudioFileFormat format = encoding::AudioFileFormat::Mp3;
    int sampleRate = 44100;
    int bitrateKbps = 192;
    bool includeMetronome = false;
    int tailMilliseconds = 500;
};

// 将取消与失败分开，便于 GUI 决定是否展示错误。
enum class AudioExportStatus { Success, Failed, Canceled };

// 帧数为每声道样本帧数；峰值/超幅统计来自合成 PCM，不代表编码后的测量值。
struct AudioExportResult {
    AudioExportStatus status = AudioExportStatus::Failed;
    QString error;
    qint64 frames = 0;
    float peakLeft = 0.0f;
    float peakRight = 0.0f;
    quint64 clippedSamples = 0;
};

// 无共享可变状态，调用方可在后台任务中执行同步导出。
class AudioExportService final {
public:
    // 进度回调运行在导出线程；GUI 通过原子采样或排队信号接收，不可直接访问控件。
    using Progress = std::function<void(int percent)>;

    // 成功合成后才完成编码器写入；取消或失败由编码器析构清理未提交输出。
    static AudioExportResult exportDocument(
        std::shared_ptr<const music::MusicDocument> document,
        const AudioExportOptions& options,
        const std::atomic_bool* canceled = nullptr,
        Progress progress = {});
};

} // namespace midi_play::app
