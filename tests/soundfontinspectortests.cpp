#include "infrastructure/audio/soundfontinspector.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include <cstdio>
#include <cstdlib>

namespace {

using midi_play::audio::SoundFontFormat;
using midi_play::audio::SoundFontInspector;

[[noreturn]] void fail(const char* message)
{
    std::fprintf(stderr, "FAILED: %s\n", message);
    std::exit(EXIT_FAILURE);
}

void require(bool condition, const char* message)
{
    if (!condition) fail(message);
}

void appendTag(QByteArray& data, const char* tag)
{
    data.append(tag, 4);
}

void appendU16(QByteArray& data, quint16 value)
{
    data.append(static_cast<char>(value & 0xff));
    data.append(static_cast<char>((value >> 8) & 0xff));
}

void appendU32(QByteArray& data, quint32 value)
{
    for (int shift = 0; shift < 32; shift += 8) {
        data.append(static_cast<char>((value >> shift) & 0xff));
    }
}

QByteArray chunk(const char* tag, QByteArray payload)
{
    QByteArray result;
    appendTag(result, tag);
    appendU32(result, static_cast<quint32>(payload.size()));
    result += payload;
    if (payload.size() % 2 != 0) result.append('\0');
    return result;
}

QByteArray list(const char* type, const QByteArray& children)
{
    QByteArray payload;
    appendTag(payload, type);
    payload += children;
    return chunk("LIST", payload);
}

QByteArray soundFont(quint16 majorVersion, bool compressedSample,
                     qsizetype sampleHeaderCount = 2)
{
    QByteArray version;
    appendU16(version, majorVersion);
    appendU16(version, 1);

    QByteArray sampleHeaders(46 * sampleHeaderCount, '\0');
    if (compressedSample) {
        sampleHeaders[44] = static_cast<char>(0x11); // mono + Ogg Vorbis
    } else {
        sampleHeaders[44] = static_cast<char>(0x01); // mono PCM
    }

    const QByteArray body = list("INFO", chunk("ifil", version))
        + list("sdta", chunk("smpl", QByteArray(2, '\0')))
        + list("pdta", chunk("shdr", sampleHeaders));
    QByteArray result;
    appendTag(result, "RIFF");
    appendU32(result, static_cast<quint32>(body.size() + 4));
    appendTag(result, "sfbk");
    result += body;
    return result;
}

QString writeFile(const QTemporaryDir& directory, const QString& name,
                  const QByteArray& contents)
{
    const QString path = QDir(directory.path()).filePath(name);
    QFile file(path);
    require(file.open(QIODevice::WriteOnly), "test SoundFont must be writable");
    require(file.write(contents) == contents.size(), "test SoundFont write must complete");
    file.close();
    return path;
}

void testSf2AndSf3AreDetectedFromContent()
{
    QTemporaryDir directory;
    require(directory.isValid(), "temporary directory must be available");

    QString error;
    const auto sf2 = SoundFontInspector::inspect(
        writeFile(directory, QStringLiteral("pcm.sf3"), soundFont(2, false)), &error);
    require(error.isEmpty() && sf2.validRiffContainer,
            "valid SF2 content must pass inspection");
    require(sf2.format == SoundFontFormat::Sf2 && !sf2.compressedSamples,
            "file suffix must not override SF2 content detection");

    const auto sf3 = SoundFontInspector::inspect(
        writeFile(directory, QStringLiteral("compressed.sf2"), soundFont(3, true)), &error);
    require(error.isEmpty() && sf3.validRiffContainer,
            "valid SF3 content must pass inspection");
    require(sf3.format == SoundFontFormat::Sf3 && sf3.compressedSamples,
            "compressed sample flags must identify SF3 content");
}

void testMalformedContainersAreRejected()
{
    QTemporaryDir directory;
    require(directory.isValid(), "temporary directory must be available");
    QString error;

    auto result = SoundFontInspector::inspect(
        writeFile(directory, QStringLiteral("empty.sf3"), {}), &error);
    require(!result.validRiffContainer && !error.isEmpty(),
            "empty SoundFont must be rejected");

    QByteArray truncated = soundFont(3, true);
    truncated.chop(12);
    result = SoundFontInspector::inspect(
        writeFile(directory, QStringLiteral("truncated.sf3"), truncated), &error);
    require(!result.validRiffContainer && !error.isEmpty(),
            "truncated RIFF data must be rejected");

    QByteArray forged = soundFont(3, true);
    forged.replace(8, 4, "WAVE");
    result = SoundFontInspector::inspect(
        writeFile(directory, QStringLiteral("forged.sf3"), forged), &error);
    require(!result.validRiffContainer && !error.isEmpty(),
            "non-SFBK RIFF data must be rejected");

    QByteArray malformedSampleHeaders = soundFont(3, true);
    const qsizetype shdrOffset = malformedSampleHeaders.indexOf("shdr");
    require(shdrOffset >= 0, "synthetic SoundFont must contain shdr");
    malformedSampleHeaders[shdrOffset + 4] = 45;
    malformedSampleHeaders[shdrOffset + 5] = 0;
    malformedSampleHeaders[shdrOffset + 6] = 0;
    malformedSampleHeaders[shdrOffset + 7] = 0;
    result = SoundFontInspector::inspect(
        writeFile(directory, QStringLiteral("malformed-shdr.sf3"), malformedSampleHeaders),
        &error);
    require(!result.validRiffContainer && !error.isEmpty(),
            "misaligned sample-header tables must be rejected");

    result = SoundFontInspector::inspect(
        writeFile(directory, QStringLiteral("oversized-shdr.sf3"),
                  soundFont(3, true, 65'537)),
        &error);
    require(!result.validRiffContainer && !error.isEmpty(),
            "oversized sample-header tables must be rejected");
}

void testRealSoundFonts(const QStringList& paths)
{
    for (const QString& path : paths) {
        QString error;
        const auto result = SoundFontInspector::inspect(path, &error);
        require(result.validRiffContainer && error.isEmpty(),
                "supplied real SoundFont must pass inspection");
    }
}

} // namespace

int main(int argc, char* argv[])
{
    QCoreApplication application(argc, argv);
    testSf2AndSf3AreDetectedFromContent();
    testMalformedContainersAreRejected();
    testRealSoundFonts(application.arguments().sliced(1));
    std::puts("SoundFont inspector tests passed");
    return EXIT_SUCCESS;
}
