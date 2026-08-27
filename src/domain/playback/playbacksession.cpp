#include "playbacksession.h"
#include <algorithm>

namespace midi_play::playback {

PlaybackSession::PlaybackSession(std::shared_ptr<const music::MusicDocument> document,
                                 std::unique_ptr<IPlaybackAudioService> audioService,
                                 QObject* parent)
    : QObject(parent), m_document(std::move(document)), m_audioService(std::move(audioService)), m_playbackModel(m_document)
{
    m_timer.setInterval(10);
    connect(&m_timer, &QTimer::timeout, this, &PlaybackSession::onTimer);
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
    setState(State::Playing);
    m_clockBaseUs = m_positionUs;
    m_clock.restart();
    m_timer.start();
}

void PlaybackSession::pause()
{
    if (m_state != State::Playing) {
        return;
    }
    m_audioService->pause();
    m_positionUs = m_clockBaseUs + m_clock.nsecsElapsed() / 1000;
    m_timer.stop();
    setState(State::Paused);
}

void PlaybackSession::stop()
{
    if (m_state == State::Empty) {
        return;
    }
    m_timer.stop();
    flushActiveNotes();
    m_audioService->stop();
    m_positionUs = 0;
    m_clockBaseUs = 0;
    m_nextTrack = 0;
    m_nextEvent = 0;
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
        m_timer.stop();
    }
    flushActiveNotes();
    m_audioService->seek(microseconds);
    m_positionUs = std::clamp<qint64>(microseconds, 0, durationMicroseconds());
    m_clockBaseUs = m_positionUs;
    const music::Tick target = m_document->microsecondsToTick(m_positionUs);
    m_nextTrack = 0;
    m_nextEvent = 0;
    while (m_nextTrack < m_playbackModel.tracks().size()) {
        const auto& events = m_playbackModel.tracks()[m_nextTrack].events;
        m_nextEvent = m_playbackModel.tracks()[m_nextTrack].index
                           ? m_playbackModel.tracks()[m_nextTrack].index->lowerBound(m_positionUs)
                           : 0;
        if (m_nextEvent < events.size() || target == 0) {
            break;
        }
        ++m_nextTrack;
        m_nextEvent = 0;
    }
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
    constexpr qint64 lookAheadUs = 40'000;
    for (int trackIndex = m_nextTrack; trackIndex < m_playbackModel.tracks().size(); ++trackIndex) {
        const auto& track = m_playbackModel.tracks()[trackIndex];
        int eventIndex = trackIndex == m_nextTrack ? m_nextEvent : 0;
        for (; eventIndex < track.events.size(); ++eventIndex) {
            const auto& event = track.events[eventIndex];
            const qint64 noteTime = event.timestampUs;
            if (noteTime >= now + lookAheadUs) {
                break;
            }
            if (noteTime >= now) {
                m_audioService->submit(event);
            }
        }
        if (trackIndex == m_nextTrack) {
            m_nextEvent = eventIndex;
        }
        if (eventIndex < track.events.size()) {
            break;
        }
        m_nextTrack = trackIndex + 1;
        m_nextEvent = 0;
    }
    if (m_positionUs >= durationMicroseconds()) {
        stop();
        return;
    }
    emitPosition();
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
