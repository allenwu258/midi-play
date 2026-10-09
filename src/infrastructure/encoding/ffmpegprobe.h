#pragma once
// FFmpeg 安装与基础 H.264/AAC MP4 能力的同步探测接口。

#include <QString>

namespace midi_play::encoding {

// valid 仅在版本、所需编码器及小规模实际封装都通过时为真。
struct FfmpegProbeResult {
    QString executablePath;
    QString version;
    QString error;
    bool valid = false;
};

// usePath 为真时从 PATH 解析，否则使用手动文件/目录；手动选择失败不会转用 PATH。
// 会启动外部进程并等待，GUI 应在后台任务中调用。
FfmpegProbeResult probeFfmpeg(const QString& configuredPath = {}, bool usePath = true);

} // namespace midi_play::encoding
