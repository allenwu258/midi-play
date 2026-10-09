// 离线 PCM 合成实现：按样本位置投递事件，固定最大块长并保留释放尾音。
#include "offlineaudiorenderer.h"
#include "domain/playback/playbackmodel.h"
#include "domain/playback/metronometimeline.h"
#include "infrastructure/audio/fluidsynthengine.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace midi_play::app {
AudioExportResult OfflineAudioRenderer::render(std::shared_ptr<const music::MusicDocument> document,
    const OfflineAudioOptions& options, Sink sink, const std::atomic_bool* canceled,
    AudioExportService::Progress progress)
{
    const auto failure = [](const QString& error) { return AudioExportResult{AudioExportStatus::Failed, error}; };
    if (!document || !document->isValid() || !sink || options.totalFrames <= 0
        || (options.sampleRate != 44100 && options.sampleRate != 48000)
        || options.ratePercent < 20 || options.ratePercent > 200)
        return failure(QStringLiteral("离线音频参数无效"));
    playback::PlaybackModel model(document);
    const auto& events = model.globalEvents();
    const qint64 durationUs = std::max(model.durationUs(), events.isEmpty() ? 0 : events.constLast().timestampUs);
    if (durationUs < 0 || durationUs > 24LL * 60 * 60 * 1'000'000)
        return failure(QStringLiteral("乐曲时长超过导出限制（24 小时）"));
    // 使用绝对音乐时间和倍率映射样本位置，避免块间累积舍入误差。
    const auto toFrame = [&options](qint64 us) {
        const qint64 denominator = 1'000'000LL * options.ratePercent;
        return (us * qint64(options.sampleRate) * 100 + denominator / 2) / denominator;
    };
    const qint64 musicalEnd = toFrame(durationUs);
    playback::MetronomeTimeline metronome(nullptr, nullptr);
    if (options.includeMetronome) {
        metronome = playback::MetronomeTimeline(document, model.timeline());
        if (!metronome.available()) return failure(metronome.unavailableReason());
    }
    audio::FluidSynthEngine engine;
    QString error;
    if (!engine.loadOffline(options.soundFontPath, options.sampleRate, options.includeMetronome, &error))
        return failure(error);
    // 两个通道数组最多各含 4096 帧，sink 借用缓冲区且必须在回调返回前消费。
    constexpr int blockFrames = 4096;
    std::vector<float> left(blockFrames), right(blockFrames);
    qsizetype eventIndex = 0, beatIndex = 0;
    qint64 current = 0;
    bool released = false;
    AudioExportResult result {AudioExportStatus::Success};
    const auto& beats = metronome.beats();
    while (current < options.totalFrames) {
        if (canceled && canceled->load()) return {AudioExportStatus::Canceled, {}, current};
        // 同一采样位置的所有事件先投递，再合成下一段音频。
        while (eventIndex < events.size() && toFrame(events[eventIndex].timestampUs) <= current)
            engine.submit(events[eventIndex++]);
        while (beatIndex < beats.size() && toFrame(beats[beatIndex].timeUs) <= current)
            engine.submitMetronomeClick(beats[beatIndex++].accent == playback::MetronomeAccent::Measure);
        // 乐曲结束时释放延音/保持踏板及音符，只执行一次，后续继续合成衰减尾音。
        if (!released && current >= musicalEnd) {
            for (int channel = 0; channel < 16; ++channel) {
                engine.controlChange(channel, 64, 0);
                engine.controlChange(channel, 66, 0);
                engine.controlChange(channel, 123, 0);
            }
            released = true;
        }
        // 块尾不能跨越下个 MIDI/节拍事件或释放边界，确保事件按样本精度生效。
        qint64 end = std::min(current + blockFrames, options.totalFrames);
        if (!released) end = std::min(end, musicalEnd);
        if (eventIndex < events.size()) end = std::min(end, toFrame(events[eventIndex].timestampUs));
        if (beatIndex < beats.size()) end = std::min(end, toFrame(beats[beatIndex].timeUs));
        const int count = int(end - current);
        if (count <= 0) return failure(QStringLiteral("音频事件时序无效"));
        if (!engine.renderOffline(count, left.data(), right.data(), &error)) return failure(error);
        // 无效浮点立即失败；记录双声道峰值和各声道超幅采样数量，不修改波形。
        for (int i = 0; i < count; ++i) {
            if (!std::isfinite(left[i]) || !std::isfinite(right[i]))
                return failure(QStringLiteral("合成器产生了无效音频采样"));
            result.peakLeft = std::max(result.peakLeft, std::abs(left[i]));
            result.peakRight = std::max(result.peakRight, std::abs(right[i]));
            result.clippedSamples += quint64(std::abs(left[i]) > 1) + quint64(std::abs(right[i]) > 1);
        }
        if (!sink(left.data(), right.data(), count, &error)) return failure(error);
        current = end;
        if (progress) progress(int(current * 100 / options.totalFrames));
    }
    result.frames = current;
    return result;
}
} // namespace midi_play::app
