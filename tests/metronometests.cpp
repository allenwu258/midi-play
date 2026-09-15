#include "domain/playback/metronometimeline.h"
#include "infrastructure/musicxml/musicxmlreader.h"
#include "infrastructure/midi/midinormalizer.h"
#include "infrastructure/midi/mididocumentbuilder.h"
#include "infrastructure/audio/fluidsynthengine.h"

#include <QCoreApplication>
#include <QTemporaryFile>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace midi_play::audio {
struct FluidSynthEngineTestAccess {
    static bool initializeOffline(FluidSynthEngine& engine, const QString& path, QString* error)
    {
        engine.m_loaded = engine.initializeSynth(path, error, true);
        return engine.m_loaded;
    }
    template<class Function>
    static Function resolve(FluidSynthEngine& engine, const char* name)
    {
        return reinterpret_cast<Function>(engine.m_library.resolve(name));
    }
    static fluid_synth_t* synth(FluidSynthEngine& engine) { return engine.m_synth; }
    static fluid_synth_t* clickSynth(FluidSynthEngine& engine) { return engine.m_metronomeSynth; }
    static int clickFont(FluidSynthEngine& engine) { return engine.m_metronomeSoundFontId; }
    static int render(FluidSynthEngine& engine, int frames, float** output)
    {
        return FluidSynthEngine::renderAudio(&engine, frames, 0, nullptr, 2, output);
    }
};
}

namespace {
using namespace midi_play;
using music::MusicDocument;
using playback::MetronomeTimeline;
using playback::MetronomeAccent;

void require(bool condition, const char* message)
{
    if (condition) return;
    std::fprintf(stderr, "FAILED: %s\n", message);
    std::exit(EXIT_FAILURE);
}

std::shared_ptr<MusicDocument> document(music::Tick duration = 3840)
{
    auto result = std::make_shared<MusicDocument>();
    result->setDuration(duration);
    result->tempos().push_back({0, 120});
    music::Track track;
    track.id = QStringLiteral("rhythm");
    music::NoteEvent note;
    note.duration = duration;
    track.notes.push_back(note);
    result->tracks().push_back(track);
    result->rebuildMeasureGrid();
    return result;
}

MetronomeTimeline timeline(const std::shared_ptr<MusicDocument>& score)
{
    return MetronomeTimeline(score, std::make_shared<music::PlaybackTimeline>(score));
}

void expectTimes(const MetronomeTimeline& grid, const QVector<qint64>& expected)
{
    require(grid.available(), qPrintable(grid.unavailableReason()));
    require(grid.beats().size() == expected.size(), "beat count");
    for (int i = 0; i < expected.size(); ++i) {
        if (grid.beats()[i].timeUs != expected[i]) {
            std::fprintf(stderr, "Beat %d: got %lld, expected %lld\n", i,
                         grid.beats()[i].timeUs, expected[i]);
            require(false, "musical beat timestamp");
        }
    }
}

void testMetersAndTempo()
{
    auto score = document();
    auto grid = timeline(score);
    expectTimes(grid, {0, 500000, 1000000, 1500000, 2000000, 2500000, 3000000, 3500000});
    require(grid.beats()[4].accent == MetronomeAccent::Measure
            && grid.beats()[3].accent == MetronomeAccent::Beat, "MIDI placeholder must derive real bars");

    score = document(2880);
    score->tracks()[0].timeSignatures = {{0, 3, 4}};
    grid = timeline(score);
    require(grid.beats().size() == 6 && grid.beats()[3].accent == MetronomeAccent::Measure, "3/4 bars");
    score->tracks()[0].timeSignatures = {{0, 6, 8}};
    expectTimes(timeline(score), {0, 750000, 1500000, 2250000});
    score->tracks()[0].timeSignatures = {{0, 6, 8, 12, 8}};
    require(timeline(score).beats().size() == 12, "SMF explicit eighth-note click overrides compound heuristic");
    score = document(2400);
    score->tracks()[0].timeSignatures = {{0, 5, 8, 0, 8, {3, 2}}};
    expectTimes(timeline(score), {0, 750000, 1250000, 2000000});

    score = document(1920);
    score->tempos().push_back({240, 60});
    expectTimes(timeline(score), {0, 750000, 1750000, 2750000});
    score->tracks()[0].timeSignatures = {{0, 4, 4, 0, 16}};
    require(timeline(score).beats().size() == 8, "SMF notated32nds scales bar length");

    score = document(2880);
    score->tracks()[0].timeSignatures = {{0, 4, 4, 36, 8}};
    expectTimes(timeline(score), {0, 750000, 1500000, 2000000, 2750000});
    require(timeline(score).beats()[3].accent == MetronomeAccent::Measure,
            "bar length must not be rounded up to a multiple of click length");
    score->tracks()[0].timeSignatures = {{0, 4, 4}, {960, 3, 4}};
    require(timeline(score).beats()[2].accent == MetronomeAccent::Measure, "meter change starts a new MIDI bar");
    score->metronomeUnits() = {{240, 0.5}};
    expectTimes(timeline(score), {0, 250000, 500000, 750000, 1000000, 1250000,
        1500000, 1750000, 2000000, 2250000, 2500000, 2750000});
}

void testClickUnitPhase()
{
    auto score = document(3840);
    score->metronomeUnits() = {{0, 1}, {240, 1}, {840, 1}, {2280, 1}};
    expectTimes(timeline(score), {0, 500000, 1000000, 1500000,
                                  2000000, 2500000, 3000000, 3500000});
    // A real tempo change changes elapsed time, never the quarter-note phase.
    score->tempos().push_back({240, 60});
    expectTimes(timeline(score), {0, 750000, 1750000, 2750000,
                                  3750000, 4750000, 5750000, 6750000});
    // Changing to an eighth at an off-grid point must wait for the next
    // eighth on the existing bar grid; it must not insert an offbeat click.
    score = document(1920);
    score->metronomeUnits() = {{0, 1}, {360, 0.5}};
    expectTimes(timeline(score), {0, 500000, 750000, 1000000, 1250000, 1500000, 1750000});

    score = document(2400);
    music::Measure pickup;
    pickup.duration = 480;
    pickup.implicit = true;
    music::Measure bar;
    bar.start = 480;
    bar.duration = 1920;
    bar.repeatStart = bar.repeatEnd = true;
    score->tracks()[0].measures = {pickup, bar};
    score->metronomeUnits() = {{0, 1}, {120, 1}, {720, 1}};
    score->rebuildMeasureGrid();
    const auto grid = timeline(score);
    expectTimes(grid, {0, 500000, 1000000, 1500000, 2000000,
                       2500000, 3000000, 3500000, 4000000});
    require(grid.beats()[0].accent == MetronomeAccent::Beat
        && grid.beats()[1].accent == MetronomeAccent::Measure
        && grid.beats()[5].accent == MetronomeAccent::Measure,
        "unit annotations preserve pickup and repeated bar accents");

    QTemporaryFile xml;
    require(xml.open(), "phase regression XML");
    xml.write(R"(<score-partwise><part-list><score-part id="P"><part-name>Piano</part-name></score-part></part-list><part id="P"><measure number="1"><attributes><divisions>2</divisions><time><beats>4</beats><beat-type>4</beat-type></time></attributes><direction><direction-type><metronome><beat-unit>quarter</beat-unit><per-minute>120</per-minute></metronome></direction-type></direction><note><pitch><step>C</step><octave>4</octave></pitch><duration>1</duration></note><direction><direction-type><metronome><beat-unit>quarter</beat-unit><per-minute>120</per-minute></metronome></direction-type></direction><note><pitch><step>D</step><octave>4</octave></pitch><duration>7</duration></note></measure></part></score-partwise>)");
    require(xml.flush(), "write phase regression XML");
    const auto read = musicxml::MusicXmlReader().read(xml.fileName());
    require(read.ok(), qPrintable(read.error));
    expectTimes(timeline(read.document), {0, 500000, 1000000, 1500000});
}

void testWrittenPickupAndRepeats()
{
    auto score = document(4320);
    music::Measure pickup;
    pickup.duration = 480;
    pickup.implicit = true;
    music::Measure first;
    first.start = 480;
    first.duration = 1920;
    first.repeatStart = true;
    music::Measure second = first;
    second.start = 2400;
    second.repeatStart = false;
    second.repeatEnd = true;
    score->tracks()[0].measures = {pickup, first, second};
    score->rebuildMeasureGrid();
    const auto grid = timeline(score);
    require(grid.beats().size() == 17, "repeat expansion must include each beat occurrence");
    require(grid.beats()[0].accent == MetronomeAccent::Beat, "pickup must not invent a downbeat");
    require(grid.beats()[1].accent == MetronomeAccent::Measure
            && grid.beats()[9].accent == MetronomeAccent::Measure
            && grid.beats()[9].timeUs == 4500000, "repeated measure accent and output time");
    score->setMusicalTimebase(false);
    require(!timeline(score).available() && timeline(score).unavailableReason().contains("SMPTE"),
            "absolute SMPTE ticks cannot pretend to be musical beats");
    score->setMusicalTimebase(true);
    score->tracks()[0].timeSignatures = {{0, 0, 4}};
    require(!timeline(score).available(), "invalid meter fails closed without affecting document playback");
}

void testScheduler()
{
    const auto grid = timeline(document());
    playback::MetronomeScheduler scheduler;
    scheduler.seek(grid, 0);
    require(scheduler.takeDue(grid, 1000, 1).has_value(), "initial beat is due");
    require(!scheduler.takeDue(grid, 2000, 1), "never replay same beat");
    require(!scheduler.takeDue(grid, 1700000, 1), "skip stale clicks after a stall");
    require(scheduler.takeDue(grid, 2010000, 1)->timeUs == 2000000, "resume at current beat after stall");
    scheduler.seek(grid, 0);
    require(scheduler.takeDue(grid, 1510000, 1)->timeUs == 1500000, "emit at most latest timely beat");
    require(!scheduler.takeDue(grid, 1510001, 1), "no burst catchup on next tick");
    scheduler.seek(grid, 500000);
    require(!scheduler.takeDue(grid, 515000, 0.2), "lateness uses wall time at 20 percent");
    scheduler.seek(grid, 500000);
    require(scheduler.takeDue(grid, 580000, 2).has_value(), "lateness uses wall time at 200 percent");
    scheduler.seek(grid, 510000);
    require(!scheduler.takeDue(grid, 515000, 1), "mid-song enable waits for the next beat");
}

void testMusicXmlMetadata()
{
    QTemporaryFile xml;
    require(xml.open(), "temporary XML");
    const QByteArray data = R"(<score-partwise version="4.0"><part-list><score-part id="P1"><part-name>Piano</part-name></score-part></part-list><part id="P1">
<measure number="0" implicit="yes"><attributes><divisions>2</divisions><time><beats>3+2</beats><beat-type>8</beat-type></time></attributes>
<direction><sound tempo="120"/><direction-type><metronome><beat-unit>quarter</beat-unit><beat-unit-dot/><per-minute>60</per-minute></metronome></direction-type></direction>
<note><pitch><step>C</step><octave>4</octave></pitch><duration>1</duration></note></measure>
<measure number="1"><direction><direction-type><metronome><beat-unit>quarter</beat-unit><beat-unit-dot/><per-minute>60</per-minute></metronome></direction-type><offset sound="yes">1</offset></direction>
<note><pitch><step>D</step><octave>4</octave></pitch><duration>5</duration></note></measure></part></score-partwise>)";
    require(xml.write(data) == data.size() && xml.flush(), "write XML");
    const auto parsed = musicxml::MusicXmlReader().read(xml.fileName());
    require(parsed.ok(), qPrintable(parsed.error));
    const auto& score = *parsed.document;
    require(score.measures()[0].implicit && score.measures()[0].duration == 240, "preserve written pickup");
    require(score.tracks()[0].timeSignatures[0].beats == 5
        && score.tracks()[0].timeSignatures[0].beatGroups == QVector<int>({3, 2}), "parse additive meter");
    require(score.tempos()[0].bpm == 120, "sound tempo wins regardless of XML order");
    require(score.tempos()[1].bpm == 90 && score.tempos()[1].tick == 480,
            "dotted quarter BPM conversion and direction offset");
    require(score.metronomeUnits()[0].quarterNotes == 1.5, "preserve explicit click unit");
}

void testMidiSequenceMetadata()
{
    midi::MidiParsedFile source;
    source.header.format = 2;
    for (int i = 0; i < 2; ++i) {
        midi::MidiRawTrack track;
        track.index = i;
        midi::MidiRawEvent on;
        on.kind = midi::MidiMessageKind::NoteOn;
        on.data1 = 60; on.data2 = 100;
        auto off = on; off.kind = midi::MidiMessageKind::NoteOff; off.tick = 1440;
        if (i == 0) {
            midi::MidiRawEvent tempo;
            tempo.kind = midi::MidiMessageKind::Meta;
            tempo.metaType = 0x51;
            tempo.payload = QByteArray::fromHex("0f4240"); // 60 BPM
            track.events.push_back(tempo);
            tempo.metaType = 0x58;
            tempo.payload = QByteArray::fromHex("06032408"); // 6/8, dotted quarter
            track.events.push_back(tempo);
        }
        track.events += {on, off};
        source.tracks.push_back(track);
    }
    const auto normalized = midi::MidiNormalizer().normalize(source);
    require(normalized.ok(), qPrintable(normalized.error));
    const auto parsed = midi::MidiDocumentBuilder().build(*normalized.file);
    require(parsed.ok(), qPrintable(parsed.error));
    const auto grid = MetronomeTimeline(parsed.document, std::make_shared<music::PlaybackTimeline>(parsed.document));
    expectTimes(grid, {0, 1500000, 3000000, 3500000, 4000000});
    require(grid.beats()[2].accent == MetronomeAccent::Measure, "format 2 resets meter phase");
}

void testFluidSynthAudio()
{
    // A tiny melodic-only user font deliberately lacks drums. Independent
    // built-in clicks must still work and must survive replacing this font.
    QFile resource(QStringLiteral(":/midi_play/audio/metronome.sf2"));
    require(resource.open(QIODevice::ReadOnly), "embedded click resource linked");
    QByteArray data = resource.readAll();
    const int header = data.indexOf("phdr");
    require(header > 0, "preset header");
    for (int i = 0; i < 4; ++i) data[header + 8 + 20 + i] = 0; // bank 0/program 0
    QTemporaryFile userFont;
    require(userFont.open() && userFont.write(data) == data.size() && userFont.flush(), "test user font");
    const auto userPath = userFont.fileName();
    userFont.close();
    audio::FluidSynthEngine engine;
    using Access = audio::FluidSynthEngineTestAccess;
    QString error;
    const bool initialized = Access::initializeOffline(engine, userPath, &error);
    require(initialized, qPrintable(error));
    const bool prepared = engine.prepareMetronome(&error);
    require(prepared && engine.supportsMetronome(), qPrintable(error));
    const int clickFont = Access::clickFont(engine);
    auto* synth = Access::synth(engine);
    auto* clickSynth = Access::clickSynth(engine);
    require(clickSynth && clickSynth != synth, "clicks must have an independent voice pool");
    const auto program = Access::resolve<int (*)(fluid_synth_t*, int, int*, int*, int*)>(engine, "fluid_synth_get_program");
    const auto cc = Access::resolve<int (*)(fluid_synth_t*, int, int, int*)>(engine, "fluid_synth_get_cc");
    require(program && cc, "offline audio inspection API");
    auto energy = [&](int frames) {
        QVector<float> left(frames), right(frames);
        float* output[] {left.data(), right.data()};
        require(Access::render(engine, frames, output) == 0, "render through the production mixing callback");
        double sum = 0;
        for (int i = 0; i < frames; ++i) sum += left[i] * left[i] + right[i] * right[i];
        return sum;
    };
    engine.submitMetronomeClick(true);
    const double strong = energy(4410);
    const double tail = energy(4410);
    engine.submitMetronomeClick(false);
    const double weak = energy(4410);
    require(strong > 0.001 && weak > 0.001 && strong > weak * 1.2, "audible distinct strong and weak clicks");
    require(tail < strong * 0.0001, "short clicks end naturally with no lingering voices/effect tail");
    engine.controlChange(0, 64, 127);
    engine.controlChange(0, 91, 87);
    engine.noteOn(0, 60, 100);
    engine.stopMetronome();
    require(energy(4410) > 0.001, "stopping clicks must preserve song voices");
    int value = 0;
    cc(synth, 0, 64, &value);
    require(value == 127, "song sustain survives metronome stop");
    cc(clickSynth, 0, 64, &value);
    require(value == 0, "private channel excludes sustain");
    cc(clickSynth, 0, 91, &value);
    require(value == 0, "private channel excludes song reverb");
    engine.flush();
    const bool replaced = engine.load(userPath, &error) && engine.prepareMetronome(&error);
    require(replaced, qPrintable(error));
    engine.flush();
    int font = -1, bank = -1, preset = -1;
    require(program(clickSynth, 0, &font, &bank, &preset) == 0
        && font == clickFont && bank == 128 && preset == 127, "restore private font after reset and replacement");
    energy(22050); // Drain any pre-existing song reverb after the reset.
    engine.submitMetronomeClick(true);
    require(energy(4410) > 0.001, "click still sounds after reset/font replacement");
    engine.submitMetronomeClick(true);
    engine.stopMetronome();
    require(energy(4410) < strong * 0.001, "stop cancels only the click voice immediately");
    std::printf("FluidSynth click energy: strong=%g weak=%g tail=%g\n", strong, weak, tail);

    // Give the test font two sample layers across the full keyboard. Fill the
    // actual song voice pool, including layered presets, before adding clicks.
    const int igen = data.indexOf("igen");
    require(igen > 0, "instrument generators");
    for (int i = 0; i < 24; ++i) {
        const int offset = igen + 8 + i * 4;
        if (static_cast<unsigned char>(data[offset]) == 43 && data[offset + 1] == 0) {
            data[offset + 2] = 0;
            data[offset + 3] = 127;
        }
    }
    QTemporaryFile layeredFont;
    require(layeredFont.open() && layeredFont.write(data) == data.size()
        && layeredFont.flush(), "layered test font");
    const auto layeredPath = layeredFont.fileName();
    layeredFont.close();
    require(engine.load(layeredPath, &error), qPrintable(error));
    engine.flush();
    energy(22050);
    const auto polyphony = Access::resolve<int (*)(fluid_synth_t*)>(engine, "fluid_synth_get_polyphony");
    const auto voiceList = Access::resolve<void (*)(fluid_synth_t*, void**, int, int)>(engine, "fluid_synth_get_voicelist");
    const auto playing = Access::resolve<int (*)(const void*)>(engine, "fluid_voice_is_playing");
    const auto voiceId = Access::resolve<unsigned int (*)(const void*)>(engine, "fluid_voice_get_id");
    require(polyphony && voiceList && playing && voiceId, "voice pressure inspection API");
    auto voices = [&](fluid_synth_t* target) {
        QVector<void*> list(polyphony(target), nullptr);
        voiceList(target, list.data(), list.size(), -1);
        QVector<unsigned int> ids;
        for (auto* voice : list) if (voice && playing(voice)) ids.push_back(voiceId(voice));
        std::sort(ids.begin(), ids.end());
        return ids;
    };
    const int capacity = polyphony(synth);
    for (int channel = 0; channel < 16 && voices(synth).size() < capacity; ++channel) {
        require(engine.configureTrack(channel, 0, &error), qPrintable(error));
        for (int key = 21; key <= 108 && voices(synth).size() < capacity; ++key)
            engine.noteOn(channel, key, 100);
    }
    const auto before = voices(synth);
    require(before.size() == capacity, "test must exhaust the real song voice pool");
    for (int i = 0; i < 100; ++i) engine.submitMetronomeClick(i % 2 == 0);
    require(!voices(clickSynth).isEmpty(), "clicks must play under song polyphony pressure");
    require(voices(synth) == before, "clicks must preserve every song voice ID at full polyphony");
    engine.stopMetronome();
    require(voices(synth) == before, "stopping overloaded clicks must preserve every song voice");
    require(energy(64) > 0.001, "song audio survives click pressure and cancellation");
    std::printf("FluidSynth polyphony isolation: preserved %d song voices through 100 clicks\n", capacity);
}
} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    if (app.arguments().contains(QStringLiteral("--audio"))) testFluidSynthAudio();
    else {
        testMetersAndTempo();
        testClickUnitPhase();
        testWrittenPickupAndRepeats();
        testScheduler();
        testMusicXmlMetadata();
        testMidiSequenceMetadata();
    }
    std::puts("metronome tests passed");
    return EXIT_SUCCESS;
}
