#include "audiofileencoder.h"

#include <QDataStream>
#include <QSaveFile>
#include <lame/lame.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

namespace midi_play::encoding {
namespace {

bool writeBytes(QSaveFile& file, const char* data, qint64 size, QString* error)
{
    qint64 written = 0;
    while (written < size) {
        const qint64 count = file.write(data + written, size - written);
        if (count <= 0) {
            if (error) *error = QStringLiteral("无法写入导出文件: %1").arg(file.errorString());
            return false;
        }
        written += count;
    }
    return true;
}

class WavEncoder final : public AudioFileEncoder {
public:
    bool open(const QString& path, int sampleRate, int, QString* error) override
    {
        m_file.setFileName(path);
        if (!m_file.open(QIODevice::WriteOnly)) {
            if (error) *error = QStringLiteral("无法创建 WAV 文件: %1").arg(m_file.errorString());
            return false;
        }
        m_sampleRate = sampleRate;
        return writeHeader(0, error);
    }

    bool write(const float* left, const float* right, int frames, QString* error) override
    {
        const quint64 bytes = static_cast<quint64>(frames) * 4;
        if (m_dataBytes + bytes > std::numeric_limits<quint32>::max() - 36) {
            if (error) *error = QStringLiteral("WAV 超过 4 GB RIFF 格式上限");
            return false;
        }
        m_pcm.resize(static_cast<size_t>(bytes));
        for (int i = 0; i < frames; ++i) {
            const auto quantize = [](float value) {
                const float limited = std::clamp(value, -1.0f, 1.0f);
                return static_cast<qint16>(std::lround(limited * (limited < 0 ? 32768.0f : 32767.0f)));
            };
            const quint16 samples[2] {static_cast<quint16>(quantize(left[i])),
                                       static_cast<quint16>(quantize(right[i]))};
            for (int channel = 0; channel < 2; ++channel) {
                m_pcm[4 * i + 2 * channel] = static_cast<char>(samples[channel] & 0xff);
                m_pcm[4 * i + 2 * channel + 1] = static_cast<char>(samples[channel] >> 8);
            }
        }
        if (!writeBytes(m_file, m_pcm.data(), static_cast<qint64>(bytes), error)) return false;
        m_dataBytes += bytes;
        return true;
    }

    bool finish(QString* error) override
    {
        if (!m_file.seek(0) || !writeHeader(static_cast<quint32>(m_dataBytes), error)) {
            if (error && error->isEmpty()) *error = QStringLiteral("无法更新 WAV 文件头");
            return false;
        }
        if (m_file.commit()) return true;
        if (error) *error = QStringLiteral("无法保存 WAV 文件: %1").arg(m_file.errorString());
        return false;
    }

private:
    bool writeHeader(quint32 dataBytes, QString* error)
    {
        QByteArray header;
        QDataStream stream(&header, QIODevice::WriteOnly);
        stream.setByteOrder(QDataStream::LittleEndian);
        stream.writeRawData("RIFF", 4);
        stream << static_cast<quint32>(36 + dataBytes);
        stream.writeRawData("WAVEfmt ", 8);
        stream << quint32(16) << quint16(1) << quint16(2)
               << quint32(m_sampleRate) << quint32(m_sampleRate * 4)
               << quint16(4) << quint16(16);
        stream.writeRawData("data", 4);
        stream << dataBytes;
        return header.size() == 44 && writeBytes(m_file, header.constData(), header.size(), error);
    }

    QSaveFile m_file;
    std::vector<char> m_pcm;
    int m_sampleRate = 0;
    quint64 m_dataBytes = 0;
};

class Mp3Encoder final : public AudioFileEncoder {
public:
    ~Mp3Encoder() override { if (m_lame) lame_close(m_lame); }

    bool open(const QString& path, int sampleRate, int bitrateKbps, QString* error) override
    {
        m_lame = lame_init();
        if (!m_lame || lame_set_in_samplerate(m_lame, sampleRate) < 0
            || lame_set_num_channels(m_lame, 2) < 0
            || lame_set_mode(m_lame, STEREO) < 0
            || lame_set_VBR(m_lame, vbr_off) < 0
            || lame_set_brate(m_lame, bitrateKbps) < 0
            || lame_set_quality(m_lame, 2) < 0
            || lame_set_bWriteVbrTag(m_lame, 1) < 0
            || lame_init_params(m_lame) < 0) {
            if (error) *error = QStringLiteral("无法初始化 LAME MP3 编码器");
            return false;
        }
        m_file.setFileName(path);
        if (m_file.open(QIODevice::WriteOnly)) return true;
        if (error) *error = QStringLiteral("无法创建 MP3 文件: %1").arg(m_file.errorString());
        return false;
    }

    bool write(const float* left, const float* right, int frames, QString* error) override
    {
        m_buffer.resize(static_cast<size_t>(frames * 5 / 4 + 7200));
        const int bytes = lame_encode_buffer_ieee_float(m_lame, left, right, frames,
                                                        m_buffer.data(), static_cast<int>(m_buffer.size()));
        if (bytes < 0) {
            if (error) *error = QStringLiteral("LAME 编码失败: %1").arg(bytes);
            return false;
        }
        return writeBytes(m_file, reinterpret_cast<const char*>(m_buffer.data()), bytes, error);
    }

    bool finish(QString* error) override
    {
        std::vector<unsigned char> buffer(7200);
        const int bytes = lame_encode_flush(m_lame, buffer.data(), static_cast<int>(buffer.size()));
        if (bytes < 0 || !writeBytes(m_file, reinterpret_cast<const char*>(buffer.data()), bytes, error)) {
            if (bytes < 0 && error) *error = QStringLiteral("LAME 结束编码失败: %1").arg(bytes);
            return false;
        }
        const size_t tagSize = lame_get_lametag_frame(m_lame, nullptr, 0);
        if (tagSize > 0) {
            std::vector<unsigned char> tag(tagSize);
            if (lame_get_lametag_frame(m_lame, tag.data(), tag.size()) != tagSize
                || !m_file.seek(0)
                || !writeBytes(m_file, reinterpret_cast<const char*>(tag.data()),
                               static_cast<qint64>(tag.size()), error)) {
                if (error && error->isEmpty()) *error = QStringLiteral("无法写入 MP3 时长与延迟信息");
                return false;
            }
        }
        if (m_file.commit()) return true;
        if (error) *error = QStringLiteral("无法保存 MP3 文件: %1").arg(m_file.errorString());
        return false;
    }

private:
    QSaveFile m_file;
    lame_t m_lame = nullptr;
    std::vector<unsigned char> m_buffer;
};

} // namespace

std::unique_ptr<AudioFileEncoder> createAudioFileEncoder(AudioFileFormat format)
{
    if (format == AudioFileFormat::Wav) return std::make_unique<WavEncoder>();
    return std::make_unique<Mp3Encoder>();
}

} // namespace midi_play::encoding
