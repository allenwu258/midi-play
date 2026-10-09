#include "soundfontinspector.h"

// 实现有界 RIFF/SFBK 扫描，识别 SF2/SF3 并拒绝残缺或越界块。
#include <QFile>
#include <QFileInfo>
#include <algorithm>

namespace midi_play::audio {
namespace {

// 将四字符块标识编码为小端整数，和文件读取结果直接比较。
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
// 限制递归深度、总块数和样本头数量，使异常容器的检查成本可控。
constexpr int kMaximumListDepth = 8;
constexpr quint64 kMaximumChunkCount = 16'384;
constexpr quint64 kMaximumSampleHeaderCount = 65'536;

// RIFF 数值使用小端字节序；读取不足时返回 false。
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

// 解码版本号和样本类型等两字节字段。
bool readU16(QFile& file, quint16* value)
{
    unsigned char bytes[2] {};
    if (file.read(reinterpret_cast<char*>(bytes), sizeof(bytes)) != sizeof(bytes)) return false;
    *value = static_cast<quint16>(bytes[0]) | (static_cast<quint16>(bytes[1]) << 8);
    return true;
}

// 块标识和 RIFF 整数字段复用同一小端读取路径。
bool readTag(QFile& file, quint32* tag)
{
    return readU32(file, tag);
}

// 仅允许定位到文件内部或末尾，拒绝负偏移和越界偏移。
bool skipTo(QFile& file, qint64 offset)
{
    return offset >= 0 && offset <= file.size() && file.seek(offset);
}

// 跨递归层累积必需块是否存在及扫描总量。
struct ScanState {
    bool sawInfo = false;
    bool sawSdta = false;
    bool sawPdta = false;
    bool sawVersion = false;
    bool sawSampleHeaders = false;
    quint64 chunkCount = 0;
};

// 扫描 [begin, end) 内的块；父 LIST 边界和上下文约束子块的大小与语义。
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
        // 先用剩余长度比较，再计算数据终点，避免不可信长度侵入父块之外。
        if (static_cast<quint64>(chunkSize) > static_cast<quint64>(end - dataOffset)) {
            return false;
        }
        const qint64 dataEnd = dataOffset + static_cast<qint64>(chunkSize);

        // LIST 的前四字节是类型，后续内容递归扫描；其他未知块仅跳过。
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
            // 最后一条是终止样本头，不作为真实样本检查。
            for (quint64 i = 0; i + 1 < recordCount; ++i) {
                const qint64 sampleTypeOffset = dataOffset + static_cast<qint64>(i * kSampleHeaderSize) + 44;
                if (!skipTo(file, sampleTypeOffset)) return false;
                quint16 sampleType = 0;
                if (!readU16(file, &sampleType)) return false;
                // 样本类型中的 Ogg Vorbis 位是压缩 SF3 的识别依据。
                if ((sampleType & kOggVorbis) != 0) {
                    result.compressedSamples = true;
                    break;
                }
            }
        }

        // RIFF 奇数长度块补一个对齐字节；补齐也不能超过父块边界。
        const qint64 padding = static_cast<qint64>(chunkSize & 1u);
        if (padding > end - dataEnd) return false;
        const qint64 next = dataEnd + padding;
        cursor = next;
    }
    return cursor == end;
}

// 统一保留路径和检查失败原因，供上层直接展示。
QString formatError(const QString& path, const QString& reason)
{
    return QStringLiteral("音源文件无效: %1 (%2)").arg(path, reason);
}

} // namespace

// 依次检查可读性、RIFF 头、声明长度与必需子块，不依赖 FluidSynth 运行时。
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

    // RIFF size 不包含前八字节；允许文件有尾随数据，但声明区间必须完整存在。
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
    // 版本 3 或压缩样本标记均归为 SF3，真正的解码能力由后端加载进一步确认。
    result.format = result.compressedSamples || result.majorVersion >= 3
        ? SoundFontFormat::Sf3 : SoundFontFormat::Sf2;
    return result;
}

} // namespace midi_play::audio
