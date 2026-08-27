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
bool FluidSynthAudioService::flush() { return m_engine->flush(); }
void FluidSynthAudioService::submit(const playback::PlaybackEvent& event)
{
    if (event.kind == playback::PlaybackEvent::Kind::ProgramChange) {
        m_engine->programChange(event.channel, event.program);
        return;
    }
    if (event.kind == playback::PlaybackEvent::Kind::ControlChange) {
        m_engine->controlChange(event.channel, event.pitch, event.velocity);
        return;
    }
    if (event.kind == playback::PlaybackEvent::Kind::AllNotesOff) {
        m_engine->flush();
        return;
    }
    m_engine->noteOn(event.channel, event.pitch, event.velocity);
    m_engine->scheduleNoteOff(event.channel, event.pitch, event.durationUs);
}

void FluidSynthAudioService::submitOff(const playback::PlaybackEvent& event)
{
    if (event.kind == playback::PlaybackEvent::Kind::AllNotesOff) {
        m_engine->flush();
        return;
    }
    m_engine->scheduleNoteOff(event.channel, event.pitch, 0);
}

} // namespace midi_play::audio
