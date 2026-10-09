#pragma once

// 音频导出参数页：将格式、采样率、码率、节拍器和尾音选项映射到应用层参数。

#include "app/audioexportservice.h"

#include <QWidget>

class QCheckBox;
class QComboBox;
class QLineEdit;
class QSpinBox;

namespace midi_play::presentation {

// 持有打开对话框时的音源路径，页面内不切换播放会话或执行音频合成。
class AudioExportPage final : public QWidget {
public:
    // 使用源文件路径生成默认输出文件名，并显示当前音源信息。
    AudioExportPage(const QString& sourcePath, const QString& soundFontPath,
                    QWidget* parent = nullptr);

    // 收集控件值；输出路径转换为绝对路径。
    app::AudioExportOptions options() const;
    // 仅判断音源路径是否已配置，文件可用性由导出服务验证。
    bool hasSoundFont() const noexcept { return !m_soundFontPath.isEmpty(); }
    // 返回去除首尾空白的输入路径。
    QString outputPath() const;
    // 预检音源配置及输出扩展名；空字符串表示通过。
    QString validationError() const;

private:
    QString m_soundFontPath;
    QComboBox* m_format = nullptr;
    QComboBox* m_bitrate = nullptr;
    QComboBox* m_sampleRate = nullptr;
    QCheckBox* m_metronome = nullptr;
    QSpinBox* m_tail = nullptr;
    QLineEdit* m_path = nullptr;
};

} // namespace midi_play::presentation
