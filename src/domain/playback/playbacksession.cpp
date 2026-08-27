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
    m_nextEvents.resize(m_playbackModel.tracks().size());
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
    if (m_state == State::Paused) {
        rebuildAudioState(m_positionUs);
    }
    setState(State::Playing);
    m_clockBaseUs = m_positionUs;
    m_clock.restart();
    m_timer->start();
}

void PlaybackSession::pause()
{
    if (m_state != State::Playing) {
        return;
    }
    m_positionUs = m_clockBaseUs + m_clock.nsecsElapsed() / 1000;
    m_timer->stop();
    // Explicit note-off events are dispatched by this session. Flush them on
    // pause and reconstruct the exact state when play() resumes.
    flushActiveNotes();
    m_audioService->pause();
    setState(State::Paused);
}

void PlaybackSession::stop()
{
    if (m_state == State::Empty) {
        return;
    }
    m_timer->stop();
    flushActiveNotes();
    m_audioService->stop();
    m_positionUs = 0;
    m_clockBaseUs = 0;
    std::fill(m_nextEvents.begin(), m_nextEvents.end(), 0);
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
    flushActiveNotes();
    m_audioService->seek(microseconds);
    m_positionUs = std::clamp<qint64>(microseconds, 0, durationMicroseconds());
    m_clockBaseUs = m_positionUs;
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
    m_positionUs = m_clockBaseUs + m_clock.nsecsElapsed() / 1000;
    const qint64 now = m_positionUs;
    for (int trackIndex = 0; trackIndex < m_playbackModel.tracks().size(); ++trackIndex) {
        const auto& track = m_playbackModel.tracks()[trackIndex];
        int eventIndex = m_nextEvents.value(trackIndex, 0);
        for (; eventIndex < track.events.size(); ++eventIndex) {
            const auto& event = track.events[eventIndex];
            if (event.timestampUs > now) {
                break;
            }
            m_audioService->submit(event);
        }
        m_nextEvents[trackIndex] = eventIndex;
    }
    if (m_positionUs >= durationMicroseconds()) {
        stop();
        return;
    }
    emitPosition();
}

void PlaybackSession::rebuildAudioState(qint64 targetUs)
{
    m_nextEvents.resize(m_playbackModel.tracks().size());
    std::fill(m_nextEvents.begin(), m_nextEvents.end(), 0);

    QHash<int, QVector<PlaybackEvent>> activeNotes;

    for (int trackIndex = 0; trackIndex < m_playbackModel.tracks().size(); ++trackIndex) {
        const auto& events = m_playbackModel.tracks()[trackIndex].events;
        const int end = m_playbackModel.tracks()[trackIndex].index
                            ? m_playbackModel.tracks()[trackIndex].index->lowerBound(targetUs)
                            : 0;
        m_nextEvents[trackIndex] = end;
        for (int i = 0; i < end; ++i) {
            const auto& event = events[i];
            if (event.kind == PlaybackEventKind::NoteOn) {
                activeNotes[event.channel * 128 + event.pitch].push_back(event);
            } else if (event.kind == PlaybackEventKind::NoteOff) {
                const int key = event.channel * 128 + event.pitch;
                if (activeNotes.contains(key) && !activeNotes[key].isEmpty()) {
                    activeNotes[key].removeLast();
                    if (activeNotes[key].isEmpty()) activeNotes.remove(key);
                }
            } else {
                // Program, controller, pitch bend and pressure events form the
                // device state that must be replayed after a seek.
                m_audioService->submit(event);
            }
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
            m_audioService->submit(resumed);
        }
    }
}

void PlaybackSession::flushActiveNotes()
{
    m_audioService->flush();
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
