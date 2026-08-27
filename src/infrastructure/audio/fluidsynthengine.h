#pragma once

#include <QLibrary>
#include <QMap>
#include <QObject>
#include <QString>

struct fluid_settings_t;
struct fluid_synth_t;
struct fluid_audio_driver_t;

namespace midi_play::audio {

class FluidSynthEngine final : public QObject {
    Q_OBJECT
public:
    explicit FluidSynthEngine(QObject* parent = nullptr);
    ~FluidSynthEngine() override;

    bool load(const QString& soundFontPath, QString* error);
    bool configureTrack(int channel, int program, QString* error);
    bool start();
    bool pause();
    bool stop();
    bool seek(qint64 microseconds);
    bool flush();
    void noteOn(int channel, int pitch, int velocity);
    void noteOff(int channel, int pitch);
    void programChange(int channel, int program);
    void controlChange(int channel, int controller, int value);
    void pitchBend(int channel, int value);
    void channelPressure(int channel, int value);

private:
    bool resolveSymbols(QString* error);
    void release();

    QLibrary m_library;
    fluid_settings_t* m_settings = nullptr;
    fluid_synth_t* m_synth = nullptr;
    fluid_audio_driver_t* m_driver = nullptr;
    int m_soundFontId = -1;
    bool m_loaded = false;

    using NewSettings = fluid_settings_t* (*)();
    using DeleteSettings = void (*)(fluid_settings_t*);
    using SettingsSetNum = int (*)(fluid_settings_t*, const char*, double);
    using SettingsSetStr = int (*)(fluid_settings_t*, const char*, const char*);
    using NewSynth = fluid_synth_t* (*)(fluid_settings_t*);
    using DeleteSynth = int (*)(fluid_synth_t*);
    using NewAudioDriver = fluid_audio_driver_t* (*)(fluid_settings_t*, fluid_synth_t*);
    using DeleteAudioDriver = void (*)(fluid_audio_driver_t*);
    using Sfload = int (*)(fluid_synth_t*, const char*, int);
    using ProgramSelect = int (*)(fluid_synth_t*, int, int, int, int);
    using NoteOn = int (*)(fluid_synth_t*, int, int, int);
    using NoteOff = int (*)(fluid_synth_t*, int, int);
    using Cc = int (*)(fluid_synth_t*, int, int, int);
    using PitchBend = int (*)(fluid_synth_t*, int, int);
    using ChannelPressure = int (*)(fluid_synth_t*, int, int);
    using SystemReset = int (*)(fluid_synth_t*);

    NewSettings m_newSettings = nullptr;
    DeleteSettings m_deleteSettings = nullptr;
    SettingsSetNum m_settingsSetNum = nullptr;
    SettingsSetStr m_settingsSetStr = nullptr;
    NewSynth m_newSynth = nullptr;
    DeleteSynth m_deleteSynth = nullptr;
    NewAudioDriver m_newAudioDriver = nullptr;
    DeleteAudioDriver m_deleteAudioDriver = nullptr;
    Sfload m_sfload = nullptr;
    ProgramSelect m_programSelect = nullptr;
    NoteOn m_noteOn = nullptr;
    NoteOff m_noteOff = nullptr;
    Cc m_cc = nullptr;
    PitchBend m_pitchBend = nullptr;
    ChannelPressure m_channelPressure = nullptr;
    SystemReset m_systemReset = nullptr;
};

} // namespace midi_play::audio
