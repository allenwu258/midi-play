#include "playbacksession.h"
#include <algorithm>
#include <QHash>

namespace midi_play::playback {

PlaybackSession::PlaybackSession(std::shared_ptr<const music::MusicDocument> document,
                                 std::unique_ptr<IPlaybackAudioService> audioService,
                                 QObject* parent)
    : QObject(parent), m_document(std::move(document)), m_audioService(std::move(audioService)), m_playbackModel(m_document)
{
    m_timer = new QTimer(this);
    m_timer->setInterval(2);
    m_timer->setTimerType(Qt::PreciseTimer);
    connect(m_timer, &QTimer::timeout, this, &PlaybackSession::onTimer);
    m_scheduler.setTracks(&m_playbackModel.tracks());
    m_scheduler.setGeneration(m_eventGeneration);
    m_playHead.configure(48000);
    m_audioService->setEventGeneration(m_eventGeneration);
}

PlaybackSession::~PlaybackSession() = default;

qint64 PlaybackSession::durationMicroseconds() const
{
    return m_playbackModel.durationUs();
}

bool PlaybackSession::loadSoundFont(const QString& path, QString* error)
{
    if (!m_audioService->loadSoundFont(path, error)) {
        return false;
    }
    for (const auto& track : m_playbackModel.tracks()) {
        if (!m_audioService->addTrack(track, error)) {
            return false;
        }
    }
    return true;
}

void PlaybackSession::play()
{
    if (m_state == State::Playing || !m_document || !m_document->isValid()) {
        return;
    }
    if (!m_audioService->start()) {
        emit errorOccurred(QStringLiteral("无法启动音频引擎"));
        setState(State::Error);
        return;
    }
    m_audioService->setTransportPosition(m_positionUs);
    if (m_state == State::Paused) {
        rebuildAudioState(m_positionUs);
    }
    setState(State::Playing);
    m_clockBaseUs = m_positionUs;
    m_playHead.start(m_positionUs);
    m_timer->start();
}

void PlaybackSession::pause()
{
    if (m_state != State::Playing) {
        return;
    }
    const qint64 audioClockUs = m_audioService->clockPositionUs();
    if (audioClockUs >= 0) m_playHead.setAudioPositionUs(audioClockUs);
    else m_playHead.clearAudioPosition();
    m_positionUs = m_playHead.positionUs();
    m_timer->stop();
    // Explicit note-off events are dispatched by this session. Flush them on
    // pause and reconstruct the exact state when play() resumes.
    flushActiveNotes();
    m_audioService->pause();
    m_playHead.pause(m_positionUs);
    setState(State::Paused);
}

void PlaybackSession::stop()
{
    if (m_state == State::Empty) {
        return;
    }
    m_timer->stop();
    advanceEventGeneration();
    flushActiveNotes();
    m_audioService->stop();
    m_positionUs = 0;
    m_clockBaseUs = 0;
    m_playHead.reset();
    m_scheduler.reset();
    emitPosition();
    setState(State::Stopped);
}

void PlaybackSession::seek(qint64 microseconds)
{
    if (!m_document) {
        return;
    }
    const bool resume = m_state == State::Playing;
    if (resume) {
        m_timer->stop();
    }
    advanceEventGeneration();
    flushActiveNotes();
    m_audioService->seek(microseconds);
    m_positionUs = std::clamp<qint64>(microseconds, 0, durationMicroseconds());
    m_clockBaseUs = m_positionUs;
    m_playHead.seek(m_positionUs);
    m_audioService->setTransportPosition(m_positionUs);
    rebuildAudioState(m_positionUs);
    emitPosition();
    if (resume) {
        play();
    }
}

void PlaybackSession::onTimer()
{
    if (m_state != State::Playing || !m_document) {
        return;
    }
    const qint64 audioClockUs = m_audioService->clockPositionUs();
    if (audioClockUs >= 0) m_playHead.setAudioPositionUs(audioClockUs);
    else m_playHead.clearAudioPosition();
    m_positionUs = m_playHead.positionUs();
    const qint64 now = m_positionUs;
    const qint64 dispatchUntilUs = m_audioService->supportsTimedEvents() ? now + 30'000 : now;
    const auto window = m_scheduler.collectWindow(now, dispatchUntilUs, m_eventGeneration);
    if (!window.events.isEmpty()) {
        m_audioService->submitBatch(window.events, window.generation);
    }
    if (m_positionUs >= durationMicroseconds()) {
        stop();
        return;
    }
    emitPosition();
}

void PlaybackSession::rebuildAudioState(qint64 targetUs)
{
    m_scheduler.seek(targetUs);

    const auto& globalEvents = m_playbackModel.globalEvents();
    const int end = m_playbackModel.globalIndex()
        ? m_playbackModel.globalIndex()->lowerBound(targetUs)
        : 0;
    int start = 0;
    QHash<int, QVector<PlaybackEvent>> activeNotes;
    QVector<PlaybackEvent> replayEvents;

    const PlaybackStateSnapshot* snapshot = nullptr;
    for (const auto& candidate : m_playbackModel.globalSnapshots()) {
        if (candidate.timestampUs > targetUs) break;
        snapshot = &candidate;
    }
    if (snapshot) {
        start = std::min(snapshot->eventIndex, end);
        for (auto channelIt = snapshot->channels.cbegin(); channelIt != snapshot->channels.cend(); ++channelIt) {
            const int channel = channelIt.key();
            const auto& state = channelIt.value();
            if (!state.initialized) continue;
            // Bank Select is a two-controller message and must precede
            // Program Change when restoring a channel after seek.
            for (const int controller : {0, 32}) {
                if (!state.controllers.contains(controller)) continue;
                PlaybackEvent control;
                control.channel = channel;
                control.controller = controller;
                control.value = state.controllers.value(controller);
                control.kind = PlaybackEventKind::ControlChange;
                replayEvents.push_back(control);
            }
            PlaybackEvent program;
            program.channel = channel;
            program.program = state.program;
            program.bankMsb = state.bankMsb;
            program.bankLsb = state.bankLsb;
            program.kind = PlaybackEventKind::ProgramChange;
            replayEvents.push_back(program);
            QVector<int> controllerKeys;
            controllerKeys.reserve(state.controllers.size());
            for (auto controllerIt = state.controllers.cbegin(); controllerIt != state.controllers.cend(); ++controllerIt) {
                if (controllerIt.key() != 0 && controllerIt.key() != 32) controllerKeys.push_back(controllerIt.key());
            }
            const auto controllerPriority = [](int controller) {
                switch (controller) {
                case 101: return 0; // RPN MSB
                case 100: return 1; // RPN LSB
                case 99: return 2;  // NRPN MSB
                case 98: return 3;  // NRPN LSB
                case 6: return 4;   // Data Entry MSB
                case 38: return 5;  // Data Entry LSB
                default: return 100 + controller;
                }
            };
            std::sort(controllerKeys.begin(), controllerKeys.end(), [&](int left, int right) {
                const int leftPriority = controllerPriority(left);
                const int rightPriority = controllerPriority(right);
                return leftPriority != rightPriority ? leftPriority < rightPriority : left < right;
            });
            for (const int controller : controllerKeys) {
                PlaybackEvent control;
                control.channel = channel;
                control.controller = controller;
                control.value = state.controllers.value(controller);
                control.kind = PlaybackEventKind::ControlChange;
                replayEvents.push_back(control);
            }
            PlaybackEvent bend;
            bend.channel = channel;
            bend.value = state.pitchBend;
            bend.kind = PlaybackEventKind::PitchBend;
            replayEvents.push_back(bend);
            PlaybackEvent pressure;
            pressure.channel = channel;
            pressure.value = state.channelPressure;
            pressure.kind = PlaybackEventKind::ChannelPressure;
            replayEvents.push_back(pressure);
        }
        for (const auto& active : snapshot->activeNotes) {
            PlaybackEvent resumed;
            resumed.timestampUs = targetUs - 1;
            resumed.durationUs = active.endTimestampUs - targetUs + 1;
            resumed.channel = active.channel;
            resumed.pitch = active.pitch;
            resumed.velocity = active.velocity;
            resumed.keyReleased = active.keyReleased;
            resumed.kind = PlaybackEventKind::NoteOn;
            activeNotes[active.channel * 128 + active.pitch].push_back(resumed);
        }
    }

    for (int i = start; i < end; ++i) {
        const auto& event = globalEvents[i];
        if (event.kind == PlaybackEventKind::NoteOn) {
            activeNotes[event.channel * 128 + event.pitch].push_back(event);
        } else if (event.kind == PlaybackEventKind::NoteOff) {
            const int key = event.channel * 128 + event.pitch;
            if (activeNotes.contains(key) && !activeNotes[key].isEmpty()) {
                activeNotes[key].removeLast();
                if (activeNotes[key].isEmpty()) activeNotes.remove(key);
            }
        } else {
            // State events after the selected global snapshot are replayed in
            // canonical score order; no track can overwrite another track's
            // shared channel state out of order.
            replayEvents.push_back(event);
        }
    }

    for (auto it = activeNotes.cbegin(); it != activeNotes.cend(); ++it) {
        for (const auto& active : it.value()) {
            PlaybackEvent resumed = active;
            const qint64 endUs = resumed.timestampUs + resumed.durationUs;
            if (endUs <= targetUs) {
                continue;
            }
            resumed.timestampUs = targetUs;
            resumed.durationUs = endUs - targetUs;
            replayEvents.push_back(resumed);
            if (active.keyReleased) {
                PlaybackEvent released = resumed;
                released.durationUs = 0;
                released.kind = PlaybackEventKind::NoteOff;
                replayEvents.push_back(released);
            }
        }
    }
    if (!replayEvents.isEmpty()) {
        m_audioService->submitBatch(replayEvents, m_eventGeneration);
    }
}

void PlaybackSession::flushActiveNotes()
{
    m_audioService->flush();
}

void PlaybackSession::advanceEventGeneration()
{
    ++m_eventGeneration;
    m_scheduler.setGeneration(m_eventGeneration);
    m_audioService->setEventGeneration(m_eventGeneration);
}

void PlaybackSession::setState(State state)
{
    if (m_state == state) {
        return;
    }
    m_state = state;
    emit stateChanged(m_state);
}

void PlaybackSession::emitPosition()
{
    emit positionChanged(m_positionUs, durationMicroseconds());
}

} // namespace midi_play::playback
