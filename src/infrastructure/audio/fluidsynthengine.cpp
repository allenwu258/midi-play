#include "fluidsynthengine.h"

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
    if (m_library.isLoaded()) return true;

#ifdef Q_OS_WIN
    const QStringList candidates { QStringLiteral("fluidsynth"), QStringLiteral("libfluidsynth-3") };
#elif defined(Q_OS_MACOS)
    const QStringList candidates { QStringLiteral("libfluidsynth.3"), QStringLiteral("fluidsynth") };
#else
    const QStringList candidates { QStringLiteral("libfluidsynth.so.3"), QStringLiteral("fluidsynth") };
#endif

    for (const QString& candidate : candidates) {
        m_library.setFileName(candidate);
        if (m_library.load()) break;
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

    if (!m_newSettings || !m_deleteSettings || !m_newSynth || !m_deleteSynth || !m_sfload
        || !m_programSelect || !m_noteOn || !m_noteOff || !m_systemReset
        || !m_newAudioDriver || !m_deleteAudioDriver) {
        if (error) *error = QStringLiteral("FluidSynth 动态库缺少必要 API");
        m_library.unload();
        return false;
    }
    return true;
}

bool FluidSynthEngine::load(const QString& soundFontPath, QString* error)
{
    release();
    if (!resolveSymbols(error)) return false;

    m_settings = m_newSettings();
    if (!m_settings) {
        if (error) *error = QStringLiteral("无法创建 FluidSynth settings");
        return false;
    }
    if (m_settingsSetNum) m_settingsSetNum(m_settings, "synth.gain", 0.8);

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

    // FluidSynth owns the realtime audio thread through its native driver.
    m_driver = m_newAudioDriver(m_settings, m_synth);
    if (!m_driver) {
        if (error) *error = QStringLiteral("无法创建 FluidSynth 音频驱动");
        release();
        return false;
    }
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
bool FluidSynthEngine::setTransportPosition(qint64) { return m_loaded; }
qint64 FluidSynthEngine::clockPositionUs() const { return -1; }
bool FluidSynthEngine::supportsTimedEvents() const { return false; }

bool FluidSynthEngine::flush()
{
    if (!m_loaded) return false;
    return m_systemReset && m_systemReset(m_synth) == 0;
}

void FluidSynthEngine::submit(const playback::PlaybackEvent& event)
{
    if (!m_loaded) return;
    switch (event.kind) {
    case playback::PlaybackEventKind::NoteOn:
        noteOn(event.channel, event.pitch, event.velocity);
        break;
    case playback::PlaybackEventKind::NoteOff:
        noteOff(event.channel, event.pitch);
        break;
    case playback::PlaybackEventKind::ProgramChange:
        programChange(event.channel, event.program, event.bankMsb, event.bankLsb);
        break;
    case playback::PlaybackEventKind::ControlChange:
        controlChange(event.channel, event.controller, event.value);
        break;
    case playback::PlaybackEventKind::PitchBend:
        pitchBend(event.channel, event.value);
        break;
    case playback::PlaybackEventKind::ChannelPressure:
        channelPressure(event.channel, event.value);
        break;
    case playback::PlaybackEventKind::NoteExpression:
        // FluidSynth exposes channel-level controls; apply the closest MIDI
        // operation while preserving the richer domain event.
        switch (event.expressionType) {
        case playback::NoteExpressionType::Pitch: pitchBend(event.channel, event.value); break;
        case playback::NoteExpressionType::Pressure: channelPressure(event.channel, event.value); break;
        case playback::NoteExpressionType::Timbre: controlChange(event.channel, 74, event.value); break;
        case playback::NoteExpressionType::Volume: controlChange(event.channel, 11, event.value); break;
        }
        break;
    case playback::PlaybackEventKind::AllNotesOff:
        flush();
        break;
    }
}

void FluidSynthEngine::noteOn(int channel, int pitch, int velocity)
{
    if (m_loaded && m_noteOn) m_noteOn(m_synth, channel % 16, pitch, velocity);
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
    if (m_driver && m_deleteAudioDriver) m_deleteAudioDriver(m_driver);
    m_driver = nullptr;
    if (m_synth && m_deleteSynth) m_deleteSynth(m_synth);
    m_synth = nullptr;
    if (m_settings && m_deleteSettings) m_deleteSettings(m_settings);
    m_settings = nullptr;
    m_soundFontId = -1;
}

} // namespace midi_play::audio
