#pragma once
// 音频/视频导出共用的离线合成器，将有序事件转换为分离双声道浮点 PCM。
#include "audioexportservice.h"

namespace midi_play::app {
// totalFrames 指定每声道样本帧总数，包括音乐结束后的释放尾音。
struct OfflineAudioOptions {
    QString soundFontPath;
    int sampleRate = 48000;
    int ratePercent = 100;
    bool includeMetronome = false;
    qint64 totalFrames = 0;
};
// 每次调用创建独立 FluidSynth 引擎，不借用实时播放设备或会话状态。
class OfflineAudioRenderer final {
public:
    // 左/右数组只在回调期间有效；count 为样本帧数，false 表示输出失败。
    using Sink = std::function<bool(const float*, const float*, int, QString*)>;
    // 同步合成，按事件边界切块；取消/进度在当前调用线程检查或执行。
    static AudioExportResult render(std::shared_ptr<const music::MusicDocument> document,
        const OfflineAudioOptions& options, Sink sink, const std::atomic_bool* canceled = nullptr,
        AudioExportService::Progress progress = {});
};
} // namespace midi_play::app
