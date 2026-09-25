#include "app/audioexportservice.h"

#include <QCoreApplication>
#include <QFile>
#include <QTemporaryDir>

#include <atomic>
#include <cstdio>
#include <cstdlib>

namespace {

void require(bool condition, const char* message)
{
    if (condition) return;
    std::fprintf(stderr, "FAILED: %s\n", message);
    std::exit(EXIT_FAILURE);
}

QByteArray readFile(const QString& path)
{
    QFile file(path);
    require(file.open(QIODevice::ReadOnly), "exported file is readable");
    return file.readAll();
}

std::shared_ptr<const midi_play::music::MusicDocument> testDocument()
{
    using namespace midi_play::music;
    auto document = std::make_shared<MusicDocument>();
    document->setDuration(960);
    document->tempos().push_back({0, 120.0, 0});
    Track track;
    track.id = QStringLiteral("click");
    track.channel = 0;
    track.program = 127;
    InstrumentChange preset;
    preset.channel = 0;
    preset.program = 127;
    preset.bankMsb = 1;
    track.instrumentChanges.push_back(preset);
    NoteEvent note;
    note.start = 240; // 250 ms at 120 BPM.
    note.duration = 120;
    note.pitch = 60;
    note.velocity = 100;
    note.noteId = 1;
    note.program = 127;
    track.notes.push_back(note);
    document->tracks().push_back(track);
    return document;
}

bool hasAudio(const QByteArray& wav, int firstFrame, int lastFrame)
{
    for (int frame = firstFrame; frame < lastFrame; ++frame) {
        const int offset = 44 + frame * 4;
        if (wav[offset] || wav[offset + 1] || wav[offset + 2] || wav[offset + 3]) return true;
    }
    return false;
}

} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    QTemporaryDir directory;
    require(directory.isValid(), "temporary output directory");
    const QString soundFont = QStringLiteral(MIDI_PLAY_TEST_CLICK_SF2);
    auto document = testDocument();
    midi_play::app::AudioExportOptions options;
    options.soundFontPath = soundFont;
    options.format = midi_play::encoding::AudioFileFormat::Wav;
    options.outputPath = directory.filePath(QStringLiteral("first.wav"));
    const auto first = midi_play::app::AudioExportService::exportDocument(document, options);
    require(first.status == midi_play::app::AudioExportStatus::Success, "WAV export succeeds");
    require(first.frames == 66'150, "WAV duration includes 500 ms release tail");
    require(first.peakLeft > 0 && first.peakRight > 0, "export reports audible peaks");
    const QByteArray wav = readFile(options.outputPath);
    require(wav.size() == 44 + first.frames * 4, "WAV header and PCM length agree");
    require(wav.startsWith("RIFF") && wav.mid(8, 4) == "WAVE", "valid RIFF/WAVE header");
    require(!hasAudio(wav, 0, 11'025), "audio is silent before scheduled note");
    require(hasAudio(wav, 11'025, 17'640), "note renders after its sample boundary");
    options.outputPath = directory.filePath(QStringLiteral("second.wav"));
    const auto second = midi_play::app::AudioExportService::exportDocument(document, options);
    require(second.status == midi_play::app::AudioExportStatus::Success,
            "second WAV export succeeds");
    require(wav == readFile(options.outputPath), "offline PCM is deterministic");

    options.format = midi_play::encoding::AudioFileFormat::Mp3;
    options.outputPath = directory.filePath(QStringLiteral("test.mp3"));
    const auto mp3 = midi_play::app::AudioExportService::exportDocument(document, options);
    require(mp3.status == midi_play::app::AudioExportStatus::Success, "MP3 export succeeds");
    const QByteArray encoded = readFile(options.outputPath);
    require(encoded.size() > 10'000, "MP3 contains encoded audio");
    require(encoded.contains("Info") || encoded.contains("Xing"),
            "MP3 includes LAME duration and gapless information");

    options.outputPath = directory.filePath(QStringLiteral("protected.mp3"));
    {
        QFile existing(options.outputPath);
        require(existing.open(QIODevice::WriteOnly), "create existing target");
        existing.write("original");
    }
    std::atomic_bool canceled = false;
    const auto interrupted = midi_play::app::AudioExportService::exportDocument(
        document, options, &canceled, [&canceled](int percent) {
            if (percent >= 10) canceled.store(true);
        });
    require(interrupted.status == midi_play::app::AudioExportStatus::Canceled,
            "cancellation is reported");
    require(readFile(options.outputPath) == "original", "cancel preserves existing target");
    options.soundFontPath = directory.filePath(QStringLiteral("missing.sf2"));
    const auto failed = midi_play::app::AudioExportService::exportDocument(document, options);
    require(failed.status == midi_play::app::AudioExportStatus::Failed,
            "missing SoundFont is rejected");
    require(readFile(options.outputPath) == "original", "failure preserves existing target");

    auto repeated = std::make_shared<midi_play::music::MusicDocument>(*document);
    midi_play::music::Measure firstMeasure;
    firstMeasure.number = 1;
    firstMeasure.start = 0;
    firstMeasure.duration = 480;
    firstMeasure.repeatStart = true;
    midi_play::music::Measure secondMeasure;
    secondMeasure.number = 2;
    secondMeasure.start = 480;
    secondMeasure.duration = 480;
    secondMeasure.repeatEnd = true;
    secondMeasure.repeatCount = 2;
    repeated->measures() = {firstMeasure, secondMeasure};
    repeated->tracks()[0].timeSignatures.push_back({0, 1, 4});
    options.soundFontPath = soundFont;
    options.format = midi_play::encoding::AudioFileFormat::Wav;
    options.outputPath = directory.filePath(QStringLiteral("repeat.wav"));
    const auto repeatResult = midi_play::app::AudioExportService::exportDocument(repeated, options);
    require(repeatResult.status == midi_play::app::AudioExportStatus::Success,
            "repeated score exports");
    require(repeatResult.frames == 110'250, "repeat expansion doubles score duration");
    const QByteArray repeatedWav = readFile(options.outputPath);
    require(hasAudio(repeatedWav, 11'025, 17'640), "first pass note renders");
    require(hasAudio(repeatedWav, 55'125, 61'740), "second pass note renders");
    options.includeMetronome = true;
    options.outputPath = directory.filePath(QStringLiteral("repeat-click.wav"));
    const auto clickResult = midi_play::app::AudioExportService::exportDocument(repeated, options);
    require(clickResult.status == midi_play::app::AudioExportStatus::Success,
            "metronome exports through independent synth");
    const QByteArray clickedWav = readFile(options.outputPath);
    require(hasAudio(clickedWav, 0, 4'410), "metronome starts on first beat");
    require(clickedWav != repeatedWav, "metronome changes mix without changing duration");
    return 0;
}
