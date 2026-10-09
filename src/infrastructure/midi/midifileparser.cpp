#include "midifileparser.h"

// 实现 SMF 边界校验与消息解码，输出仍使用源 tick 的原始事件。
#include <QFile>
#include <QFileInfo>

#include <algorithm>
#include <limits>

namespace midi_play::midi {
namespace {

// 有界字节游标；只借用缓冲区，读取函数通过 bool 报告数据不足。
class ByteReader final {
public:
    // 缓冲区必须存活到读取器销毁。
    explicit ByteReader(const QByteArray& bytes) : m_bytes(bytes) {}

    // 返回当前偏移、剩余长度并检查下一次访问是否在缓冲区内。
    qsizetype position() const { return m_position; }
    qsizetype remaining() const { return m_bytes.size() - m_position; }
    bool canRead(qsizetype count) const { return count >= 0 && count <= remaining(); }

    // 按无符号字节读取，避免 QByteArray 的有符号 char 改变状态码。
    bool readU8(quint8& value)
    {
        if (!canRead(1)) return false;
        value = static_cast<quint8>(m_bytes.at(m_position++));
        return true;
    }

    // SMF 整数字段使用大端字节序。
    bool readU16(quint16& value)
    {
        quint8 a = 0;
        quint8 b = 0;
        if (!readU8(a) || !readU8(b)) return false;
        value = (quint16(a) << 8) | quint16(b);
        return true;
    }

    // 解码四字节大端长度字段。
    bool readU32(quint32& value)
    {
        quint8 a = 0;
        quint8 b = 0;
        quint8 c = 0;
        quint8 d = 0;
        if (!readU8(a) || !readU8(b) || !readU8(c) || !readU8(d)) return false;
        value = (quint32(a) << 24) | (quint32(b) << 16) | (quint32(c) << 8) | quint32(d);
        return true;
    }

    // 复制指定长度的负载并推进游标。
    bool readBytes(qsizetype count, QByteArray& value)
    {
        if (!canRead(count)) return false;
        value = m_bytes.mid(m_position, count);
        m_position += count;
        return true;
    }

    // 跳过无需解释的字段，仍执行边界校验。
    bool skip(qsizetype count)
    {
        if (!canRead(count)) return false;
        m_position += count;
        return true;
    }

    // 每字节贡献 7 位；SMF VLQ 最多四字节，末字节的续接位必须为零。
    bool readVlq(quint32& value)
    {
        value = 0;
        for (int index = 0; index < 4; ++index) {
            quint8 byte = 0;
            if (!readU8(byte)) return false;
            value = (value << 7) | (byte & 0x7f);
            if ((byte & 0x80) == 0) return true;
        }
        return false;
    }

private:
    const QByteArray& m_bytes;
    qsizetype m_position = 0;
};

// 按 UTF-8 解码轨道名或文本元事件，不依赖负载以空字符结尾。
QString textFromPayload(const QByteArray& payload)
{
    return QString::fromUtf8(payload.constData(), payload.size());
}

// 解析单个 MTrk；sequence 跨轨道递增，保留同一时间点的原始消息顺序。
bool parseTrack(ByteReader& reader, int trackIndex, MidiRawTrack& track, quint64& sequence, QString& error)
{
    QByteArray marker;
    quint32 chunkLength = 0;
    if (!reader.readBytes(4, marker) || marker != QByteArrayLiteral("MTrk") || !reader.readU32(chunkLength)) {
        error = QStringLiteral("MIDI track chunk 无效");
        return false;
    }
    QByteArray chunk;
    if (!reader.readBytes(chunkLength, chunk)) {
        error = QStringLiteral("MIDI track 超出文件范围");
        return false;
    }

    // 用独立缓冲区限制轨道读取范围，畸形消息不能侵入下一轨道。
    ByteReader trackReader(chunk);
    track.index = trackIndex;
    music::Tick tick = 0;
    quint8 runningStatus = 0;
    int currentPort = 0;

    while (trackReader.remaining() > 0) {
        quint32 delta = 0;
        if (!trackReader.readVlq(delta)) {
            error = QStringLiteral("MIDI delta-time VLQ 无效");
            return false;
        }
        // delta-time 累加为绝对 tick，先检查溢出再推进时间。
        if (tick > std::numeric_limits<music::Tick>::max() - static_cast<music::Tick>(delta)) {
            error = QStringLiteral("MIDI 时间轴溢出");
            return false;
        }
        tick += static_cast<music::Tick>(delta);

        quint8 statusOrData = 0;
        if (!trackReader.readU8(statusOrData)) {
            error = QStringLiteral("MIDI 事件缺少状态字节");
            return false;
        }

        quint8 status = statusOrData;
        quint8 firstData = 0;
        bool hasFirstData = false;
        // Running Status 复用最近的通道状态；当前字节已经是第一个数据字节。
        if (statusOrData < 0x80) {
            if (runningStatus < 0x80 || runningStatus >= 0xf0) {
                error = QStringLiteral("MIDI Running Status 缺少前置状态");
                return false;
            }
            status = runningStatus;
            firstData = statusOrData;
            hasFirstData = true;
        } else if (status >= 0x80 && status < 0xf0) {
            runningStatus = status;
        }

        // 元事件带独立的类型与 VLQ 长度；端口标记只影响后续消息。
        if (status == 0xff) {
            quint8 metaType = 0;
            quint32 length = 0;
            QByteArray payload;
            if (!trackReader.readU8(metaType) || !trackReader.readVlq(length)
                || !trackReader.readBytes(length, payload)) {
                error = QStringLiteral("MIDI Meta Event 长度无效");
                return false;
            }
            MidiRawEvent event;
            event.sourceTrack = trackIndex;
            event.port = currentPort;
            event.tick = tick;
            event.sequence = sequence++;
            event.kind = MidiMessageKind::Meta;
            event.metaType = metaType;
            event.payload = payload;
            track.events.push_back(event);
            if (metaType == 0x21 && !payload.isEmpty()) {
                currentPort = static_cast<unsigned char>(payload.at(0));
            }
            // 此解析器在元事件后清除 Running Status，轨道结束标记立即终止解析。
            runningStatus = 0;
            if (metaType == 0x03 && !payload.isEmpty()) track.name = textFromPayload(payload);
            if (metaType == 0x01 && track.name.isEmpty() && !payload.isEmpty()) track.name = textFromPayload(payload);
            if (metaType == 0x2f) {
                break;
            }
            continue;
        }

        // SysEx 只保留原始负载，不在导入时向合成器执行设备指令。
        if (status == 0xf0 || status == 0xf7) {
            quint32 length = 0;
            QByteArray payload;
            if (!trackReader.readVlq(length) || !trackReader.readBytes(length, payload)) {
                error = QStringLiteral("MIDI SysEx 长度无效");
                return false;
            }
            MidiRawEvent event;
            event.sourceTrack = trackIndex;
            event.port = currentPort;
            event.tick = tick;
            event.sequence = sequence++;
            event.kind = MidiMessageKind::SysEx;
            event.payload = payload;
            track.events.push_back(event);
            runningStatus = 0;
            continue;
        }

        // 对遇到的系统公共/实时消息按固定长度消费负载，不加入音符时间线。
        // 未识别状态或残缺负载直接报错，防止后续通道消息发生字节错位。
        if (status >= 0xf0) {
            int dataLength = 0;
            switch (status) {
            case 0xf1:
            case 0xf3:
                dataLength = 1;
                break;
            case 0xf2:
                dataLength = 2;
                break;
            case 0xf6:
            case 0xf8:
            case 0xf9:
            case 0xfa:
            case 0xfb:
            case 0xfc:
            case 0xfd:
            case 0xfe:
                dataLength = 0;
                break;
            default:
                error = QStringLiteral("未知 MIDI 系统消息: 0x%1").arg(status, 2, 16, QLatin1Char('0'));
                return false;
            }
            if (!trackReader.skip(dataLength)) {
                error = QStringLiteral("MIDI 系统消息数据不完整");
                return false;
            }
            runningStatus = 0;
            continue;
        }

        const quint8 command = status & 0xf0;
        if (status < 0x80) {
            error = QStringLiteral("MIDI 系统消息不受支持或格式无效");
            return false;
        }
        const int channel = status & 0x0f;
        // Program Change 和 Channel Pressure 仅有一个数据字节。
        const bool oneDataByte = command == 0xc0 || command == 0xd0;
        quint8 data1 = firstData;
        quint8 data2 = 0;
        if (!hasFirstData && !trackReader.readU8(data1)) {
            error = QStringLiteral("MIDI Channel Event 缺少第一个数据字节");
            return false;
        }
        if (!oneDataByte && !trackReader.readU8(data2)) {
            error = QStringLiteral("MIDI Channel Event 缺少第二个数据字节");
            return false;
        }

        MidiRawEvent event;
        event.sourceTrack = trackIndex;
        event.port = currentPort;
        event.channel = channel;
        event.tick = tick;
        event.sequence = sequence++;
        event.data1 = data1;
        event.data2 = data2;
        // 统一零力度 NoteOn 和 NoteOff，并把弯音两字节合并为 14 位值。
        switch (command) {
        case 0x80: event.kind = MidiMessageKind::NoteOff; break;
        case 0x90: event.kind = data2 == 0 ? MidiMessageKind::NoteOff : MidiMessageKind::NoteOn; break;
        case 0xa0: event.kind = MidiMessageKind::PolyPressure; break;
        case 0xb0: event.kind = MidiMessageKind::ControlChange; break;
        case 0xc0: event.kind = MidiMessageKind::ProgramChange; break;
        case 0xd0: event.kind = MidiMessageKind::ChannelPressure; break;
        case 0xe0: event.kind = MidiMessageKind::PitchBend; event.data2 = (int(data2) << 7) | int(data1); break;
        default:
            error = QStringLiteral("未知 MIDI Channel Event");
            return false;
        }
        track.events.push_back(event);
    }

    return true;
}

} // namespace

// 校验文件头和声明的轨道数量，完整解析后才返回中间文件。
MidiParseResult MidiFileParser::parse(const QString& path) const
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {nullptr, file.errorString()};
    const QByteArray bytes = file.readAll();
    ByteReader reader(bytes);

    QByteArray marker;
    quint32 headerLength = 0;
    quint16 format = 0;
    quint16 trackCount = 0;
    quint16 division = 0;
    if (!reader.readBytes(4, marker) || marker != QByteArrayLiteral("MThd")
        || !reader.readU32(headerLength) || headerLength < 6 || !reader.canRead(headerLength)
        || !reader.readU16(format) || !reader.readU16(trackCount) || !reader.readU16(division)) {
        return {nullptr, QStringLiteral("MIDI header 无效")};
    }
    if (format > 2) return {nullptr, QStringLiteral("不支持的 MIDI format: %1").arg(format)};
    if (division == 0) return {nullptr, QStringLiteral("MIDI division 无效")};
    if (headerLength > 6 && !reader.skip(headerLength - 6)) return {nullptr, QStringLiteral("MIDI header 超出文件范围")};

    auto result = std::make_shared<MidiParsedFile>();
    result->header.format = format;
    result->header.trackCount = trackCount;
    // division 最高位区分 PPQ 与 SMPTE；后者的高字节表示负帧率。
    if ((division & 0x8000) == 0) {
        result->header.division = division;
    } else {
        const int signedFps = static_cast<qint8>((division >> 8) & 0xff);
        const int fps = std::abs(signedFps);
        const int ticksPerFrame = division & 0xff;
        if (fps <= 0 || ticksPerFrame <= 0) return {nullptr, QStringLiteral("SMPTE division 无效")};
        result->header.smpte = true;
        // 帧率字段将 29 记为 30；缩放仍使用下方 fps * ticksPerFrame 的近似基准。
        result->header.smpteFramesPerSecond = fps == 29 ? 30 : fps;
        result->header.ticksPerFrame = ticksPerFrame;
        result->header.division = fps * ticksPerFrame;
    }

    quint64 sequence = 0;
    result->tracks.reserve(trackCount);
    for (int index = 0; index < trackCount; ++index) {
        MidiRawTrack track;
        QString error;
        if (!parseTrack(reader, index, track, sequence, error)) return {nullptr, error};
        if (index == 0 && !track.name.isEmpty()) result->title = track.name;
        result->tracks.push_back(std::move(track));
    }
    return {std::move(result), {}};
}

} // namespace midi_play::midi
