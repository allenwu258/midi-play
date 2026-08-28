#include "domain/playback/iplaybackaudioservice.h"
#include "domain/playback/playbacksession.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QThread>

#include <cstdio>
#include <cstdlib>
#include <memory>

namespace {

using midi_play::music::MusicDocument;
using midi_play::music::NoteEvent;
using midi_play::music::Track;
using midi_play::playback::IPlaybackAudioService;
using midi_play::playback::PlaybackData;
using midi_play::playback::PlaybackEvent;
using midi_play::playback::PlaybackEventKind;
using midi_play::playback::PlaybackSession;
using midi_play::playback::State;

[[noreturn]] void fail(const char* message)
{
    std::fprintf(stderr, "FAILED: %s\n", message);
    std::exit(EXIT_FAILURE);
}

void require(bool condition, const char* message)
{
    if (!condition) fail(message);
}

class RecordingAudioService final : public IPlaybackAudioService {
public:
    bool loadSoundFont(const QString&, QString*) override { return true; }
    bool configureTrack(const QString&, int, int, QString*) override { return true; }
    bool addTrack(const PlaybackData&, QString*) override { return true; }
    bool start() override
    {
        ++startCount;
        return true;
    }
    bool pause() override
    {
        ++pauseCount;
        return true;
    }
    bool stop() override { return true; }
    bool seek(qint64 positionUs) override
    {
        seekPositions.push_back(positionUs);
        return true;
    }
    bool setTransportPosition(qint64 positionUs) override
    {
        transportPositions.push_back(positionUs);
        return true;
    }
    bool flush() override
    {
        ++flushCount;
        return true;
    }
    void submit(const PlaybackEvent& event) override { submittedEvents.push_back(event); }
    void submitOff(const PlaybackEvent& event) override { submittedEvents.push_back(event); }
    void submitBatch(const QVector<PlaybackEvent>& events, quint64 generation) override
    {
        submittedGenerations.push_back(generation);
        submittedEvents += events;
    }
    void setEventGeneration(quint64 generation) override { currentGeneration = generation; }

    void clearSubmissions()
    {
        submittedEvents.clear();
        submittedGenerations.clear();
    }

    int startCount = 0;
    int pauseCount = 0;
    int flushCount = 0;
    quint64 currentGeneration = 0;
    QVector<qint64> seekPositions;
    QVector<qint64> transportPositions;
    QVector<quint64> submittedGenerations;
    QVector<PlaybackEvent> submittedEvents;
};

std::shared_ptr<const MusicDocument> testDocument()
{
    auto document = std::make_shared<MusicDocument>();
    document->tempos().push_back({0, 120.0, 0});
    document->setDuration(1'920); // Two seconds at 120 BPM.

    Track track;
    track.id = QStringLiteral("transport-test");
    NoteEvent note;
    note.noteId = 1;
    note.start = 0;
    note.duration = 1'920;
    note.pitch = 60;
    track.notes.push_back(note);
    document->tracks().push_back(track);
    document->rebuildMeasureGrid();
    return document;
}

bool waitUntil(const std::function<bool()>& predicate, int timeoutMs = 500)
{
    QElapsedTimer timeout;
    timeout.start();
    while (!predicate() && timeout.elapsed() < timeoutMs) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
        QThread::msleep(1);
    }
    return predicate();
}

void processEventsFor(int durationMs)
{
    QElapsedTimer duration;
    duration.start();
    while (duration.elapsed() < durationMs) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
        QThread::msleep(1);
    }
}

bool containsNoteOn(const QVector<PlaybackEvent>& events)
{
    for (const auto& event : events) {
        if (event.kind == PlaybackEventKind::NoteOn) return true;
    }
    return false;
}

void testSeekPreservesTransportIntent()
{
    auto audio = std::make_unique<RecordingAudioService>();
    auto* recording = audio.get();
    PlaybackSession session(testDocument(), std::move(audio));

    session.play();
    require(session.state() == State::Playing, "play must enter Playing state");
    require(waitUntil([&] { return session.positionMicroseconds() > 0; }),
            "playing session must advance before seek");

    constexpr qint64 playingSeekTargetUs = 1'000'000;
    recording->clearSubmissions();
    session.seek(playingSeekTargetUs);
    require(session.state() == State::Playing, "playing seek must preserve Playing state");
    require(session.positionMicroseconds() == playingSeekTargetUs,
            "playing seek must publish the release position immediately");
    require(recording->startCount == 2, "playing seek must restart audio scheduling");
    require(containsNoteOn(recording->submittedEvents),
            "playing seek must restore a note spanning the target position");
    require(waitUntil([&] { return session.positionMicroseconds() > playingSeekTargetUs; }),
            "playing seek must continue advancing without a pause/play cycle");

    session.pause();
    require(session.state() == State::Paused, "pause must enter Paused state");
    constexpr qint64 pausedSeekTargetUs = 500'000;
    recording->clearSubmissions();
    session.seek(pausedSeekTargetUs);
    require(session.state() == State::Paused, "paused seek must preserve Paused state");
    require(session.positionMicroseconds() == pausedSeekTargetUs,
            "paused seek must move to the release position");
    require(recording->submittedEvents.isEmpty(),
            "paused seek must not submit controller or note events");
    processEventsFor(30);
    require(session.positionMicroseconds() == pausedSeekTargetUs,
            "paused seek position must remain stationary");

    session.play();
    require(session.state() == State::Playing, "play after paused seek must resume playback");
    require(containsNoteOn(recording->submittedEvents),
            "play after paused seek must restore a spanning note");
    require(waitUntil([&] { return session.positionMicroseconds() > pausedSeekTargetUs; }),
            "play after paused seek must advance from the selected position");
}

} // namespace

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    testSeekPreservesTransportIntent();
    std::fprintf(stdout, "playback session tests passed\n");
    return EXIT_SUCCESS;
}
