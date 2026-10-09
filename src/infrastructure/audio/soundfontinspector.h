#pragma once

// 在调用 FluidSynth 前检查 SoundFont 的 RIFF 容器与格式线索。
#include <QString>

namespace midi_play::audio {

// 根据容器版本和样本类型识别的音源格式，不依赖文件扩展名。
enum class SoundFontFormat {
    Unknown,
    Sf2,
    Sf3
};

// 分阶段填写的检查结果；容器有效不等同于样本已成功解码。
struct SoundFontInspection {
    QString path;
    SoundFontFormat format = SoundFontFormat::Unknown;
    bool readable = false;
    bool validRiffContainer = false; // 必需块存在且扫描未越界时才置为 true。
    bool compressedSamples = false;
    int majorVersion = 0;
    int minorVersion = 0;
    qint64 fileSize = 0;
};

// 只扫描容器与样本头，不加载合成器或完整采样数据。
class SoundFontInspector final {
public:
    // 返回绝对路径、大小和格式；失败时保留已获得的信息并填写可选错误。
    static SoundFontInspection inspect(const QString& path, QString* error = nullptr);
};

} // namespace midi_play::audio
