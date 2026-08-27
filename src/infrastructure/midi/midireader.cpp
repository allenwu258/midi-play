#include "midireader.h"

#include <QFile>
#include <QHash>

namespace midi_play::midi {
namespace {

struct ByteReader {
    const QByteArray& data;
    qsizetype pos = 0;

    bool canRead(qsizetype count) const { return pos + count <= data.size(); }
    quint8 u8() { return canRead(1) ? static_cast<quint8>(data.at(pos++)) : 0; }
    quint32 u32() { return (quint32(u8()) << 24) | (quint32(u8()) << 16) | (quint32(u8()) << 8) | quint32(u8()); }
    QByteArray bytes(qsizetype count) { const QByteArray result = data.mid(pos, count); pos += count; return result; }
    quint32 vlq()
    {
        quint32 result = 0;
        for (int i = 0; i < 4; ++i) {
            const quint8 value = u8();
            result = (result << 7) | (value & 0x7f);
            if ((value & 0x80) == 0) break;
        }
        return result;
    }
};

struct ActiveNote { music::Tick tick = 0; int velocity = 90; };

} // namespace

musicxml::ReadResult MidiReader::read(const QString& path) const
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {nullptr, file.errorString()};
    const QByteArray bytes = file.readAll();
    ByteReader reader {bytes};
    if (!reader.canRead(14) || reader.bytes(4) != QByteArrayLiteral("MThd")) {
        return {nullptr, QStringLiteral("不是标准 MIDI 文件")};
    }
    const quint32 headerLength = reader.u32();
    if (headerLength < 6 || !reader.canRead(headerLength)) return {nullptr, QStringLiteral("MIDI header 无效")};
    const quint16 format = (quint16(reader.u8()) << 8) | reader.u8();
    const quint16 trackCount = (quint16(reader.u8()) << 8) | reader.u8();
    const quint16 division = (quint16(reader.u8()) << 8) | reader.u8();
    if (format > 2 || division == 0 || (division & 0x8000)) return {nullptr, QStringLiteral("暂不支持 SMPTE MIDI timing")};
    if (headerLength > 6) reader.bytes(headerLength - 6);

    auto document = std::make_shared<music::MusicDocument>();
    music::Tick maxTick = 0;
    for (quint16 trackIndex = 0; trackIndex < trackCount && reader.canRead(8); ++trackIndex) {
        if (reader.bytes(4) != QByteArrayLiteral("MTrk")) return {nullptr, QStringLiteral("MIDI track chunk 无效")};
        const quint32 length = reader.u32();
        if (!reader.canRead(length)) return {nullptr, QStringLiteral("MIDI track 超出文件范围")};
        const qsizetype trackEnd = reader.pos + length;
        music::Track track;
        track.id = QStringLiteral("midi-track-%1").arg(trackIndex + 1);
        track.name = track.id;
        QHash<int, ActiveNote> active;
        music::Tick tick = 0;
        quint8 runningStatus = 0;
        int currentProgram = 0;
        while (reader.pos < trackEnd) {
            tick += reader.vlq();
            quint8 status = reader.u8();
            if (status < 0x80) {
                if (runningStatus == 0) break;
                --reader.pos;
                status = runningStatus;
            } else if (status < 0xf0) {
                runningStatus = status;
            }
            if (status == 0xff) {
                const quint8 metaType = reader.u8();
                const quint32 size = reader.vlq();
                if (metaType == 0x51 && size == 3 && reader.canRead(3)) {
                    const quint32 usPerQuarter = (quint32(reader.u8()) << 16) | (quint32(reader.u8()) << 8) | reader.u8();
                    if (usPerQuarter > 0) document->tempos().push_back({tick * music::MusicDocument::kPpq / division,
                                                                          60'000'000.0 / usPerQuarter});
                } else {
                    reader.bytes(size);
                }
                if (metaType == 0x2f) break;
                continue;
            }
            if (status == 0xf0 || status == 0xf7) {
                reader.bytes(reader.vlq());
                continue;
            }
            const int command = status & 0xf0;
            const int channel = status & 0x0f;
            const int first = reader.u8();
            const int second = (command == 0xc0 || command == 0xd0) ? 0 : reader.u8();
            if (command == 0xc0) {
                currentProgram = first;
                track.program = currentProgram;
                track.channel = channel;
            } else if (command == 0x90 && second > 0) {
                active.insert(channel * 128 + first, {tick, second});
            } else if (command == 0x80 || (command == 0x90 && second == 0)) {
                const int key = channel * 128 + first;
                if (active.contains(key)) {
                    const auto start = active.take(key);
                    const music::Tick scaledStart = start.tick * music::MusicDocument::kPpq / division;
                    const music::Tick scaledEnd = tick * music::MusicDocument::kPpq / division;
                    track.notes.push_back({scaledStart, qMax<music::Tick>(1, scaledEnd - scaledStart), first,
                                           start.velocity, channel, currentProgram});
                    maxTick = qMax(maxTick, scaledEnd);
                }
            }
        }
        reader.pos = trackEnd;
        if (!track.notes.isEmpty()) document->tracks().push_back(std::move(track));
    }
    document->setDuration(maxTick);
    if (document->tempos().isEmpty()) document->tempos().push_back({0, 120.0});
    if (!document->isValid()) return {nullptr, QStringLiteral("MIDI 未包含可播放音符")};
    return {std::move(document), {}};
}

} // namespace midi_play::midi
