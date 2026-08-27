#include "fluidsynthaudioservice.h"

namespace midi_play::audio {

FluidSynthAudioService::FluidSynthAudioService(std::unique_ptr<FluidSynthEngine> engine)
    : m_engine(std::move(engine))
{
}

bool FluidSynthAudioService::loadSoundFont(const QString& path, QString* error) { return m_engine->load(path, error); }
bool FluidSynthAudioService::configureTrack(const QString&, int channel, int program, QString* error) { return m_engine->configureTrack(channel, program, error); }
bool FluidSynthAudioService::addTrack(const playback::PlaybackData& data, QString* error)
{
    return configureTrack(data.trackId, data.events.isEmpty() ? 0 : data.events.front().channel,
                          data.events.isEmpty() ? 0 : data.events.front().program, error);
}
bool FluidSynthAudioService::start() { return m_engine->start(); }
bool FluidSynthAudioService::pause() { return m_engine->pause(); }
bool FluidSynthAudioService::stop() { return m_engine->stop(); }
bool FluidSynthAudioService::seek(qint64 positionUs) { return m_engine->seek(positionUs); }
bool FluidSynthAudioService::setTransportPosition(qint64 positionUs) { return m_engine->setTransportPosition(positionUs); }
qint64 FluidSynthAudioService::clockPositionUs() const { return m_engine->clockPositionUs(); }
playback::PlaybackClockSource FluidSynthAudioService::clockSource() const
{
    return m_engine->supportsTimedEvents() ? playback::PlaybackClockSource::TimedSequencer
                                           : playback::PlaybackClockSource::SoftwareMonotonic;
}
bool FluidSynthAudioService::supportsTimedEvents() const { return m_engine->supportsTimedEvents(); }
bool FluidSynthAudioService::supportsPerNoteExpression() const { return false; }
bool FluidSynthAudioService::flush() { return m_engine->flush(); }
void FluidSynthAudioService::submit(const playback::PlaybackEvent& event)
{
    m_engine->submit(event);
}

void FluidSynthAudioService::submitOff(const playback::PlaybackEvent& event)
{
    m_engine->submit(event);
}

} // namespace midi_play::audio
