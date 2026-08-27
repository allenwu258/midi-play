#include "fluidsynthengine.h"

#include <QCoreApplication>
#include <QtGlobal>

#include <algorithm>

namespace midi_play::audio {

FluidSynthEngine::FluidSynthEngine(QObject* parent)
    : QObject(parent)
{
}

FluidSynthEngine::~FluidSynthEngine()
{
    release();
}

bool FluidSynthEngine::resolveSymbols(QString* error)
{
    if (m_library.isLoaded()) {
        return true;
    }

#ifdef Q_OS_WIN
    const QStringList candidates { QStringLiteral("fluidsynth"), QStringLiteral("libfluidsynth-3") };
#elif defined(Q_OS_MACOS)
    const QStringList candidates { QStringLiteral("libfluidsynth.3"), QStringLiteral("fluidsynth") };
#else
    const QStringList candidates { QStringLiteral("libfluidsynth.so.3"), QStringLiteral("fluidsynth") };
#endif

    for (const QString& candidate : candidates) {
        m_library.setFileName(candidate);
        if (m_library.load()) {
            break;
        }
    }
    if (!m_library.isLoaded()) {
        if (error) {
            *error = QStringLiteral("未找到 FluidSynth 动态库。请安装 fluidsynth 并确保其位于 PATH。详情: %1")
                         .arg(m_library.errorString());
        }
        return false;
    }

    auto resolve = [this](const char* name) { return m_library.resolve(name); };
    m_newSettings = reinterpret_cast<NewSettings>(resolve("new_fluid_settings"));
    m_deleteSettings = reinterpret_cast<DeleteSettings>(resolve("delete_fluid_settings"));
    m_settingsSetNum = reinterpret_cast<SettingsSetNum>(resolve("fluid_settings_setnum"));
    m_settingsSetStr = reinterpret_cast<SettingsSetStr>(resolve("fluid_settings_setstr"));
    m_newSynth = reinterpret_cast<NewSynth>(resolve("new_fluid_synth"));
    m_deleteSynth = reinterpret_cast<DeleteSynth>(resolve("delete_fluid_synth"));
    m_newAudioDriver = reinterpret_cast<NewAudioDriver>(resolve("new_fluid_audio_driver"));
    m_deleteAudioDriver = reinterpret_cast<DeleteAudioDriver>(resolve("delete_fluid_audio_driver"));
    m_sfload = reinterpret_cast<Sfload>(resolve("fluid_synth_sfload"));
    m_programSelect = reinterpret_cast<ProgramSelect>(resolve("fluid_synth_program_select"));
    m_noteOn = reinterpret_cast<NoteOn>(resolve("fluid_synth_noteon"));
    m_noteOff = reinterpret_cast<NoteOff>(resolve("fluid_synth_noteoff"));
    m_cc = reinterpret_cast<Cc>(resolve("fluid_synth_cc"));
    m_pitchBend = reinterpret_cast<PitchBend>(resolve("fluid_synth_pitch_bend"));
    m_channelPressure = reinterpret_cast<ChannelPressure>(resolve("fluid_synth_channel_pressure"));
    m_systemReset = reinterpret_cast<SystemReset>(resolve("fluid_synth_system_reset"));
    m_newSequencer = reinterpret_cast<NewSequencer>(resolve("new_fluid_sequencer2"));
    m_deleteSequencer = reinterpret_cast<DeleteSequencer>(resolve("delete_fluid_sequencer"));
    m_registerSynth = reinterpret_cast<RegisterSynth>(resolve("fluid_sequencer_register_fluidsynth"));
    m_newEvent = reinterpret_cast<NewEvent>(resolve("new_fluid_event"));
    m_deleteEvent = reinterpret_cast<DeleteEvent>(resolve("delete_fluid_event"));
    m_eventSetDest = reinterpret_cast<EventSetDest>(resolve("fluid_event_set_dest"));
    m_eventNoteOn = reinterpret_cast<EventNoteOn>(resolve("fluid_event_noteon"));
    m_eventNoteOff = reinterpret_cast<EventNoteOff>(resolve("fluid_event_noteoff"));
    m_eventAllNotesOff = reinterpret_cast<EventAllNotesOff>(resolve("fluid_event_all_notes_off"));
    m_eventProgramSelect = reinterpret_cast<EventProgramSelect>(resolve("fluid_event_program_select"));
    m_eventControlChange = reinterpret_cast<EventControlChange>(resolve("fluid_event_control_change"));
    m_eventPitchBend = reinterpret_cast<EventPitchBend>(resolve("fluid_event_pitch_bend"));
    m_eventChannelPressure = reinterpret_cast<EventChannelPressure>(resolve("fluid_event_channel_pressure"));
    m_sequencerSendAt = reinterpret_cast<SequencerSendAt>(resolve("fluid_sequencer_send_at"));
    m_sequencerGetTick = reinterpret_cast<SequencerGetTick>(resolve("fluid_sequencer_get_tick"));
    m_sequencerRemoveEvents = reinterpret_cast<SequencerRemoveEvents>(resolve("fluid_sequencer_remove_events"));

    if (!m_newSettings || !m_deleteSettings || !m_newSynth || !m_deleteSynth || !m_sfload
        || !m_programSelect || !m_noteOn || !m_noteOff || !m_systemReset || !m_newAudioDriver || !m_deleteAudioDriver) {
        if (error) {
            *error = QStringLiteral("FluidSynth 动态库缺少必要 API");
        }
        m_library.unload();
        return false;
    }
    return true;
}

bool FluidSynthEngine::load(const QString& soundFontPath, QString* error)
{
    release();
    if (!resolveSymbols(error)) {
        return false;
    }
    m_settings = m_newSettings();
    if (!m_settings) {
        if (error) *error = QStringLiteral("无法创建 FluidSynth settings");
        return false;
    }
    if (m_settingsSetNum) {
        m_settingsSetNum(m_settings, "synth.gain", 0.8);
    }
    m_synth = m_newSynth(m_settings);
    if (!m_synth) {
        if (error) *error = QStringLiteral("无法创建 FluidSynth synthesizer");
        release();
        return false;
    }
    m_soundFontId = m_sfload(m_synth, soundFontPath.toUtf8().constData(), 1);
    if (m_soundFontId < 0) {
        if (error) *error = QStringLiteral("无法加载 SF2: %1").arg(soundFontPath);
        release();
        return false;
    }
    m_driver = m_newAudioDriver(m_settings, m_synth);
    if (!m_driver) {
        if (error) *error = QStringLiteral("无法创建 FluidSynth 音频驱动");
        release();
        return false;
    }
    m_useTimedSequencer = qEnvironmentVariableIsSet("MIDI_PLAY_FLUID_TIMED_SEQUENCER");
    if (m_useTimedSequencer && m_newSequencer && m_registerSynth && m_newEvent && m_deleteEvent
        && m_eventSetDest && m_sequencerSendAt) {
        m_sequencer = m_newSequencer(1);
        if (m_sequencer) {
            m_sequencerDestination = m_registerSynth(m_sequencer, m_synth);
        }
    }
    m_transportTick = 0;
    m_transportPositionUs = 0;
    // The system-timer sequencer is deprecated by FluidSynth and is not
    // reliable across all Windows audio-driver builds. It is opt-in.
    m_loaded = true;
    return true;
}

bool FluidSynthEngine::configureTrack(int channel, int program, QString* error)
{
    if (!m_loaded || !m_programSelect) {
        if (error) *error = QStringLiteral("音频引擎尚未加载 SF2");
        return false;
    }
    const int result = m_programSelect(m_synth, channel % 16, m_soundFontId, 0, program % 128);
    if (result != 0) {
        if (error) *error = QStringLiteral("无法设置 MIDI program: %1").arg(program);
        return false;
    }
    return true;
}

bool FluidSynthEngine::start() { return m_loaded; }
bool FluidSynthEngine::pause() { return m_loaded; }
bool FluidSynthEngine::stop() { return flush(); }
bool FluidSynthEngine::seek(qint64) { return flush(); }

bool FluidSynthEngine::setTransportPosition(qint64 microseconds)
{
    if (!m_loaded) return false;
    m_transportPositionUs = std::max<qint64>(0, microseconds);
    if (m_sequencer && m_sequencerGetTick) {
        m_transportTick = m_sequencerGetTick(m_sequencer);
        m_transportTickAtSet = m_transportTick;
    }
    return true;
}

qint64 FluidSynthEngine::clockPositionUs() const
{
    if (!m_useTimedSequencer || !m_sequencer || !m_sequencerGetTick) return -1;
    const unsigned int tick = m_sequencerGetTick(m_sequencer);
    if (tick < m_transportTickAtSet) return m_transportPositionUs;
    return m_transportPositionUs + static_cast<qint64>(tick - m_transportTickAtSet) * 1000;
}

bool FluidSynthEngine::supportsTimedEvents() const
{
    return m_useTimedSequencer && m_sequencer != nullptr;
}

bool FluidSynthEngine::flush()
{
    if (!m_loaded) return false;
    if (m_sequencer && m_sequencerRemoveEvents) {
        m_sequencerRemoveEvents(m_sequencer, -1, m_sequencerDestination, -1);
    }
    return m_systemReset && m_systemReset(m_synth) == 0;
}

void FluidSynthEngine::submit(const playback::PlaybackEvent& event)
{
    if (!m_loaded) return;
    const auto dispatchImmediate = [this, &event] {
        switch (event.kind) {
        case playback::PlaybackEventKind::NoteOn: noteOn(event.channel, event.pitch, event.velocity); break;
        case playback::PlaybackEventKind::NoteOff: noteOff(event.channel, event.pitch); break;
        case playback::PlaybackEventKind::ProgramChange:
            programChange(event.channel, event.program, event.bankMsb, event.bankLsb);
            break;
        case playback::PlaybackEventKind::ControlChange: controlChange(event.channel, event.controller, event.value); break;
        case playback::PlaybackEventKind::PitchBend: pitchBend(event.channel, event.value); break;
        case playback::PlaybackEventKind::ChannelPressure: channelPressure(event.channel, event.value); break;
        case playback::PlaybackEventKind::AllNotesOff: flush(); break;
        }
    };
    if (!m_useTimedSequencer || !m_sequencer || m_sequencerDestination < 0 || !m_newEvent || !m_deleteEvent
        || !m_eventSetDest || !m_sequencerSendAt) {
        dispatchImmediate();
        return;
    }

    fluid_event_t* nativeEvent = m_newEvent();
    if (!nativeEvent) return;
    m_eventSetDest(nativeEvent, m_sequencerDestination);
    switch (event.kind) {
    case playback::PlaybackEventKind::NoteOn:
        m_eventNoteOn(nativeEvent, event.channel % 16, static_cast<short>(event.pitch), static_cast<short>(event.velocity));
        break;
    case playback::PlaybackEventKind::NoteOff:
        m_eventNoteOff(nativeEvent, event.channel % 16, static_cast<short>(event.pitch));
        break;
    case playback::PlaybackEventKind::ProgramChange:
        m_eventProgramSelect(nativeEvent, event.channel % 16, static_cast<unsigned int>(m_soundFontId),
                             static_cast<unsigned int>((std::clamp(event.bankMsb, 0, 127) << 7)
                                                       | std::clamp(event.bankLsb, 0, 127)),
                             static_cast<short>(event.program));
        break;
    case playback::PlaybackEventKind::ControlChange:
        m_eventControlChange(nativeEvent, event.channel % 16, static_cast<short>(event.controller), event.value);
        break;
    case playback::PlaybackEventKind::PitchBend:
        m_eventPitchBend(nativeEvent, event.channel % 16, event.value);
        break;
    case playback::PlaybackEventKind::ChannelPressure:
        m_eventChannelPressure(nativeEvent, event.channel % 16, event.value);
        break;
    case playback::PlaybackEventKind::AllNotesOff:
        m_eventAllNotesOff(nativeEvent, event.channel % 16);
        break;
    }
    const qint64 relativeUs = std::max<qint64>(0, event.timestampUs - m_transportPositionUs);
    const unsigned int targetTick = m_transportTickAtSet
                                  + static_cast<unsigned int>(relativeUs / 1000);
    const unsigned int currentTick = m_sequencerGetTick ? m_sequencerGetTick(m_sequencer) : m_transportTickAtSet;
    if (targetTick <= currentTick + 1) {
        m_deleteEvent(nativeEvent);
        dispatchImmediate();
        return;
    }
    const int sendResult = m_sequencerSendAt(m_sequencer, nativeEvent, targetTick, 1);
    m_deleteEvent(nativeEvent);
    if (sendResult != 0) {
        dispatchImmediate();
    }
}

void FluidSynthEngine::noteOn(int channel, int pitch, int velocity)
{
    if (m_loaded) {
        m_noteOn(m_synth, channel % 16, pitch, velocity);
    }
}

void FluidSynthEngine::noteOff(int channel, int pitch)
{
    if (m_loaded && m_noteOff) m_noteOff(m_synth, channel % 16, pitch);
}

void FluidSynthEngine::programChange(int channel, int program, int bankMsb, int bankLsb)
{
    if (m_loaded && m_programSelect) {
        const unsigned int bank = static_cast<unsigned int>((std::clamp(bankMsb, 0, 127) << 7)
                                                            | std::clamp(bankLsb, 0, 127));
        m_programSelect(m_synth, channel % 16, m_soundFontId, bank, program % 128);
    }
}

void FluidSynthEngine::controlChange(int channel, int controller, int value)
{
    if (m_loaded && m_cc) m_cc(m_synth, channel % 16, controller % 128, value % 128);
}

void FluidSynthEngine::pitchBend(int channel, int value)
{
    if (m_loaded && m_pitchBend) m_pitchBend(m_synth, channel % 16, std::clamp(value, 0, 16383));
}

void FluidSynthEngine::channelPressure(int channel, int value)
{
    if (m_loaded && m_channelPressure) m_channelPressure(m_synth, channel % 16, std::clamp(value, 0, 127));
}

void FluidSynthEngine::release()
{
    m_loaded = false;
    if (m_sequencer && m_deleteSequencer) m_deleteSequencer(m_sequencer);
    m_sequencer = nullptr;
    m_sequencerDestination = -1;
    if (m_driver && m_deleteAudioDriver) m_deleteAudioDriver(m_driver);
    m_driver = nullptr;
    if (m_synth && m_deleteSynth) m_deleteSynth(m_synth);
    m_synth = nullptr;
    if (m_settings && m_deleteSettings) m_deleteSettings(m_settings);
    m_settings = nullptr;
    m_soundFontId = -1;
}

} // namespace midi_play::audio
