#pragma once

// 离线音频导出的分块编码接口；输入为分离的左右声道浮点 PCM。
#include <QString>
#include <memory>

namespace midi_play::encoding {

// WAV 输出 16 位立体声 PCM，MP3 使用 LAME 固定码率编码。
enum class AudioFileFormat { Wav, Mp3 };

// 一次导出按 open -> write（可多次）-> finish 顺序使用；finish 成功才提交目标文件。
class AudioFileEncoder {
public:
    // 允许通过接口释放具体编码器及其未提交资源。
    virtual ~AudioFileEncoder() = default;
    // 初始化输出与采样率；bitrateKbps 仅对 MP3 生效。
    virtual bool open(const QString& path, int sampleRate, int bitrateKbps, QString* error) = 0;
    // 编码 frames 个立体声采样帧；调用方负责提供有效缓冲区和样本。
    virtual bool write(const float* left, const float* right, int frames, QString* error) = 0;
    // 刷新尾部、回填格式元数据并提交输出；失败通过 error 返回。
    virtual bool finish(QString* error) = 0;
};

// 按格式创建独立编码实例，避免应用层依赖 LAME 或 RIFF 细节。
std::unique_ptr<AudioFileEncoder> createAudioFileEncoder(AudioFileFormat format);

} // namespace midi_play::encoding
