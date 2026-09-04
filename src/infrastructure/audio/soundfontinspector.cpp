#include "soundfontinspector.h"

#include <QFile>
#include <QFileInfo>
#include <algorithm>

namespace midi_play::audio {
namespace {

constexpr quint32 makeTag(char a, char b, char c, char d)
{
    return static_cast<quint32>(static_cast<unsigned char>(a))
        | (static_cast<quint32>(static_cast<unsigned char>(b)) << 8)
        | (static_cast<quint32>(static_cast<unsigned char>(c)) << 16)
        | (static_cast<quint32>(static_cast<unsigned char>(d)) << 24);
}

constexpr quint32 kRiff = makeTag('R', 'I', 'F', 'F');
constexpr quint32 kSfbk = makeTag('s', 'f', 'b', 'k');
constexpr quint32 kList = makeTag('L', 'I', 'S', 'T');
constexpr quint32 kInfo = makeTag('I', 'N', 'F', 'O');
constexpr quint32 kSdta = makeTag('s', 'd', 't', 'a');
constexpr quint32 kPdta = makeTag('p', 'd', 't', 'a');
constexpr quint32 kIfil = makeTag('i', 'f', 'i', 'l');
constexpr quint32 kShdr = makeTag('s', 'h', 'd', 'r');
constexpr quint32 kOggVorbis = 0x10;
constexpr qint64 kChunkHeaderSize = 8;
constexpr qint64 kSoundFontHeaderSize = 12;
constexpr qint64 kSampleHeaderSize = 46;
constexpr int kMaximumListDepth = 8;
constexpr quint64 kMaximumChunkCount = 16'384;
constexpr quint64 kMaximumSampleHeaderCount = 65'536;

bool readU32(QFile& file, quint32* value)
{
    unsigned char bytes[4] {};
    if (file.read(reinterpret_cast<char*>(bytes), sizeof(bytes)) != sizeof(bytes)) return false;
    *value = static_cast<quint32>(bytes[0])
        | (static_cast<quint32>(bytes[1]) << 8)
        | (static_cast<quint32>(bytes[2]) << 16)
        | (static_cast<quint32>(bytes[3]) << 24);
    return true;
}

bool readU16(QFile& file, quint16* value)
{
    unsigned char bytes[2] {};
    if (file.read(reinterpret_cast<char*>(bytes), sizeof(bytes)) != sizeof(bytes)) return false;
    *value = static_cast<quint16>(bytes[0]) | (static_cast<quint16>(bytes[1]) << 8);
    return true;
}

bool readTag(QFile& file, quint32* tag)
{
    return readU32(file, tag);
}

bool skipTo(QFile& file, qint64 offset)
{
    return offset >= 0 && offset <= file.size() && file.seek(offset);
}

struct ScanState {
    bool sawInfo = false;
    bool sawSdta = false;
    bool sawPdta = false;
    bool sawVersion = false;
    bool sawSampleHeaders = false;
    quint64 chunkCount = 0;
};

bool scanChunks(QFile& file, qint64 begin, qint64 end, quint32 listContext,
                SoundFontInspection& result, ScanState& state, int depth)
{
    if (begin < 0 || end < begin || end > file.size() || depth > kMaximumListDepth) {
        return false;
    }
    qint64 cursor = begin;
    while (cursor < end) {
        if (++state.chunkCount > kMaximumChunkCount) return false;
        if (end - cursor < kChunkHeaderSize || !skipTo(file, cursor)) return false;
        quint32 tag = 0;
        quint32 chunkSize = 0;
        if (!readTag(file, &tag) || !readU32(file, &chunkSize)) return false;
        const qint64 dataOffset = cursor + kChunkHeaderSize;
        if (static_cast<quint64>(chunkSize) > static_cast<quint64>(end - dataOffset)) {
            return false;
        }
        const qint64 dataEnd = dataOffset + static_cast<qint64>(chunkSize);

        if (tag == kList) {
            if (chunkSize < 4 || !skipTo(file, dataOffset)) return false;
            quint32 listType = 0;
            if (!readTag(file, &listType)) return false;
            if (listType == kInfo) state.sawInfo = true;
            if (listType == kSdta) state.sawSdta = true;
            if (listType == kPdta) state.sawPdta = true;
            const qint64 nestedBegin = dataOffset + 4;
            if (!scanChunks(file, nestedBegin, dataEnd, listType, result, state, depth + 1)) {
                return false;
            }
        } else if (tag == kIfil && listContext == kInfo) {
            if (chunkSize < 4 || !skipTo(file, dataOffset)) return false;
            quint16 major = 0;
            quint16 minor = 0;
            if (!readU16(file, &major) || !readU16(file, &minor)) return false;
            result.majorVersion = std::max(result.majorVersion, static_cast<int>(major));
            result.minorVersion = static_cast<int>(minor);
            state.sawVersion = true;
        } else if (tag == kShdr && listContext == kPdta) {
            if (chunkSize < kSampleHeaderSize || chunkSize % kSampleHeaderSize != 0
                || !skipTo(file, dataOffset)) {
                return false;
            }
            state.sawSampleHeaders = true;
            const quint64 recordCount = chunkSize / kSampleHeaderSize;
            if (recordCount > kMaximumSampleHeaderCount) return false;
            // The last record is the mandatory terminal sample header.
            for (quint64 i = 0; i + 1 < recordCount; ++i) {
                const qint64 sampleTypeOffset = dataOffset + static_cast<qint64>(i * kSampleHeaderSize) + 44;
                if (!skipTo(file, sampleTypeOffset)) return false;
                quint16 sampleType = 0;
                if (!readU16(file, &sampleType)) return false;
                if ((sampleType & kOggVorbis) != 0) {
                    result.compressedSamples = true;
                    break;
                }
            }
        }

        // RIFF chunks are padded to an even byte boundary.
        const qint64 padding = static_cast<qint64>(chunkSize & 1u);
        if (padding > end - dataEnd) return false;
        const qint64 next = dataEnd + padding;
        cursor = next;
    }
    return cursor == end;
}

QString formatError(const QString& path, const QString& reason)
{
    return QStringLiteral("音源文件无效: %1 (%2)").arg(path, reason);
}

} // namespace

SoundFontInspection SoundFontInspector::inspect(const QString& path, QString* error)
{
    if (error) error->clear();
    SoundFontInspection result;
    result.path = QFileInfo(path).absoluteFilePath();

    QFile file(result.path);
    const QFileInfo fileInfo(result.path);
    if (!fileInfo.exists() || !fileInfo.isFile() || !file.open(QIODevice::ReadOnly)) {
        if (error) *error = formatError(path, QStringLiteral("文件不可读"));
        return result;
    }
    result.readable = true;
    result.fileSize = file.size();
    if (result.fileSize < kSoundFontHeaderSize) {
        if (error) *error = formatError(path, QStringLiteral("文件头不完整"));
        return result;
    }

    quint32 riff = 0;
    quint32 declaredSize = 0;
    quint32 form = 0;
    if (!readTag(file, &riff) || !readU32(file, &declaredSize) || !readTag(file, &form)
        || riff != kRiff || form != kSfbk) {
        if (error) *error = formatError(path, QStringLiteral("不是 RIFF/SFBK SoundFont"));
        return result;
    }

    const quint64 declaredEnd = 8ull + declaredSize;
    if (declaredEnd < kSoundFontHeaderSize || declaredEnd > static_cast<quint64>(result.fileSize)) {
        if (error) *error = formatError(path, QStringLiteral("RIFF 大小字段越界"));
        return result;
    }

    ScanState state;
    if (!scanChunks(file, kSoundFontHeaderSize, static_cast<qint64>(declaredEnd), kSfbk,
                    result, state, 0)
        || !state.sawInfo || !state.sawSdta || !state.sawPdta
        || !state.sawVersion || !state.sawSampleHeaders) {
        if (error) {
            *error = formatError(path,
                QStringLiteral("SoundFont 必要的 INFO、sdta、pdta、ifil 或 shdr 数据不完整"));
        }
        return result;
    }

    result.validRiffContainer = true;
    result.format = result.compressedSamples || result.majorVersion >= 3
        ? SoundFontFormat::Sf3 : SoundFontFormat::Sf2;
    return result;
}

} // namespace midi_play::audio
