#include "playbackplayhead.h"

#include <algorithm>

namespace midi_play::playback {

void PlaybackPlayHead::configure(int sampleRate)
{
    m_sampleRate = std::max(1, sampleRate);
}

void PlaybackPlayHead::reset()
{
    m_clock.reset();
    m_audioPositionUs = -1;
    m_state = PlayHeadState::Stopped;
}

void PlaybackPlayHead::start(qint64 positionUs)
{
    m_audioPositionUs = -1;
    m_clock.start(positionUs);
    m_state = PlayHeadState::Playing;
}

void PlaybackPlayHead::pause(qint64 positionUs)
{
    const qint64 resolved = std::max<qint64>(0, positionUs);
    m_clock.pause(resolved);
    m_audioPositionUs = resolved;
    m_state = PlayHeadState::Paused;
}

void PlaybackPlayHead::seek(qint64 positionUs)
{
    const qint64 resolved = std::max<qint64>(0, positionUs);
    m_clock.seek(resolved);
    m_audioPositionUs = resolved;
}

void PlaybackPlayHead::setAudioPositionUs(qint64 positionUs)
{
    if (positionUs < 0) {
        m_audioPositionUs = -1;
        return;
    }
    m_audioPositionUs = positionUs;
}

void PlaybackPlayHead::clearAudioPosition()
{
    m_audioPositionUs = -1;
}

qint64 PlaybackPlayHead::positionUs() const
{
    if (m_audioPositionUs >= 0) return m_audioPositionUs;
    return m_clock.positionUs();
}

qint64 PlaybackPlayHead::framePosition() const
{
    return positionUs() * static_cast<qint64>(m_sampleRate) / 1'000'000;
}

} // namespace midi_play::playback
