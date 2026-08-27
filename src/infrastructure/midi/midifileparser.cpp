#include "midifileparser.h"

#include <QFile>
#include <QFileInfo>

#include <algorithm>
#include <limits>

namespace midi_play::midi {
namespace {

class ByteReader final {
public:
    explicit ByteReader(const QByteArray& bytes) : m_bytes(bytes) {}

    qsizetype position() const { return m_position; }
    qsizetype remaining() const { return m_bytes.size() - m_position; }
    bool canRead(qsizetype count) const { return count >= 0 && count <= remaining(); }

    bool readU8(quint8& value)
    {
        if (!canRead(1)) return false;
        value = static_cast<quint8>(m_bytes.at(m_position++));
        return true;
    }

    bool readU16(quint16& value)
    {
        quint8 a = 0;
        quint8 b = 0;
        if (!readU8(a) || !readU8(b)) return false;
        value = (quint16(a) << 8) | quint16(b);
        return true;
    }

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

    bool readBytes(qsizetype count, QByteArray& value)
    {
        if (!canRead(count)) return false;
        value = m_bytes.mid(m_position, count);
        m_position += count;
        return true;
    }

    bool skip(qsizetype count)
    {
        if (!canRead(count)) return false;
        m_position += count;
        return true;
    }

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

QString textFromPayload(const QByteArray& payload)
{
    return QString::fromUtf8(payload.constData(), payload.size());
}

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
            runningStatus = 0;
            if (metaType == 0x03 && !payload.isEmpty()) track.name = textFromPayload(payload);
            if (metaType == 0x01 && track.name.isEmpty() && !payload.isEmpty()) track.name = textFromPayload(payload);
            if (metaType == 0x2f) {
                break;
            }
            continue;
        }

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

        // System common/realtime messages are legal in SMF files but have no
        // effect on the note timeline. Consume their payload explicitly so a
        // malformed or unsupported system message cannot desynchronise the
        // following channel events.
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
    if ((division & 0x8000) == 0) {
        result->header.division = division;
    } else {
        const int signedFps = static_cast<qint8>((division >> 8) & 0xff);
        const int fps = std::abs(signedFps);
        const int ticksPerFrame = division & 0xff;
        if (fps <= 0 || ticksPerFrame <= 0) return {nullptr, QStringLiteral("SMPTE division 无效")};
        result->header.smpte = true;
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
