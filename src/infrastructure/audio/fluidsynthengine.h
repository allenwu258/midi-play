#pragma once

#include "domain/playback/playbacktypes.h"
#include "infrastructure/audio/soundfontinspector.h"

#include <QLibrary>
#include <QMap>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTemporaryFile>
#include <memory>

struct fluid_settings_t;
struct fluid_synth_t;
struct fluid_audio_driver_t;

namespace midi_play::audio {

enum class BackendFeatureSupport {
    Unknown,
    Unsupported,
    Supported
};

struct FluidSynthCapabilities {
    QString version;
    bool supportsSf2 = true;
    BackendFeatureSupport supportsSf3 = BackendFeatureSupport::Unknown;
};

class FluidSynthEngine final : public QObject {
    Q_OBJECT
public:
    explicit FluidSynthEngine(QObject* parent = nullptr);
    ~FluidSynthEngine() override;

    bool validateSoundFont(const QString& soundFontPath, QString* error);
    bool load(const QString& soundFontPath, QString* error);
    FluidSynthCapabilities capabilities() const;
    bool configureTrack(int channel, int program, QString* error);
    bool start();
    bool pause();
    bool stop();
    bool seek(qint64 microseconds);
    bool setTransportPosition(qint64 microseconds);
    qint64 clockPositionUs() const;
    bool supportsTimedEvents() const;
    bool supportsMetronome() const;
    bool prepareMetronome(QString* error);
    void submitMetronomeClick(bool accent);
    void stopMetronome();
    bool flush();
    void submit(const playback::PlaybackEvent& event);
    void noteOn(int channel, int pitch, int velocity);
    void noteOff(int channel, int pitch);
    void programChange(int channel, int program, int bankMsb = 0, int bankLsb = 0);
    void controlChange(int channel, int controller, int value);
    void pitchBend(int channel, int value);
    void channelPressure(int channel, int value);
    void polyPressure(int channel, int pitch, int value);

private:
    // Offline integration tests use the real synth without opening a device.
    friend struct FluidSynthEngineTestAccess;
    bool resolveSymbols(QString* error);
    bool initializeSynth(const QString& soundFontPath, QString* error,
                         bool dynamicSampleLoading = true);
    bool loadSoundFontIntoActiveSynth(const QString& soundFontPath, QString* error);
    bool loadIntoSynth(const QString& soundFontPath, int resetPresets, int* soundFontId,
                       QString* error);
    void updateFormatCapability(SoundFontFormat format, bool loaded, const QStringList& logs);
    void release();
    bool configureMetronomeChannel();

    QLibrary m_library;
    fluid_settings_t* m_settings = nullptr;
    fluid_synth_t* m_synth = nullptr;
    fluid_audio_driver_t* m_driver = nullptr;
    int m_soundFontId = -1;
    int m_metronomeSoundFontId = -1;
    std::unique_ptr<QTemporaryFile> m_metronomeFile;
    bool m_metronomeChannelsReserved = false;
    bool m_metronomeReady = false;
    bool m_loaded = false;
    FluidSynthCapabilities m_capabilities;

    using NewSettings = fluid_settings_t* (*)();
    using DeleteSettings = void (*)(fluid_settings_t*);
    using SettingsSetNum = int (*)(fluid_settings_t*, const char*, double);
    using SettingsSetInt = int (*)(fluid_settings_t*, const char*, int);
    using SettingsSetStr = int (*)(fluid_settings_t*, const char*, const char*);
    using FluidLogFunction = void (*)(int, const char*, void*);
    using SetLogFunction = FluidLogFunction (*)(int, FluidLogFunction, void*);
    using VersionString = const char* (*)();
    using NewSynth = fluid_synth_t* (*)(fluid_settings_t*);
    using DeleteSynth = int (*)(fluid_synth_t*);
    using NewAudioDriver = fluid_audio_driver_t* (*)(fluid_settings_t*, fluid_synth_t*);
    using DeleteAudioDriver = void (*)(fluid_audio_driver_t*);
    using Sfload = int (*)(fluid_synth_t*, const char*, int);
    using Sfunload = int (*)(fluid_synth_t*, int, int);
    using ProgramSelect = int (*)(fluid_synth_t*, int, int, int, int);
    using NoteOn = int (*)(fluid_synth_t*, int, int, int);
    using NoteOff = int (*)(fluid_synth_t*, int, int);
    using Cc = int (*)(fluid_synth_t*, int, int, int);
    using PitchBend = int (*)(fluid_synth_t*, int, int);
    using ChannelPressure = int (*)(fluid_synth_t*, int, int);
    using KeyPressure = int (*)(fluid_synth_t*, int, int, int);
    using SystemReset = int (*)(fluid_synth_t*);
    using SetChannelType = int (*)(fluid_synth_t*, int, int);
    using AllSoundsOff = int (*)(fluid_synth_t*, int);
    using CountMidiChannels = int (*)(fluid_synth_t*);

    NewSettings m_newSettings = nullptr;
    DeleteSettings m_deleteSettings = nullptr;
    SettingsSetNum m_settingsSetNum = nullptr;
    SettingsSetInt m_settingsSetInt = nullptr;
    SettingsSetStr m_settingsSetStr = nullptr;
    SetLogFunction m_setLogFunction = nullptr;
    VersionString m_versionString = nullptr;
    NewSynth m_newSynth = nullptr;
    DeleteSynth m_deleteSynth = nullptr;
    NewAudioDriver m_newAudioDriver = nullptr;
    DeleteAudioDriver m_deleteAudioDriver = nullptr;
    Sfload m_sfload = nullptr;
    Sfunload m_sfunload = nullptr;
    ProgramSelect m_programSelect = nullptr;
    NoteOn m_noteOn = nullptr;
    NoteOff m_noteOff = nullptr;
    Cc m_cc = nullptr;
    PitchBend m_pitchBend = nullptr;
    ChannelPressure m_channelPressure = nullptr;
    KeyPressure m_keyPressure = nullptr;
    SystemReset m_systemReset = nullptr;
    SetChannelType m_setChannelType = nullptr;
    AllSoundsOff m_allSoundsOff = nullptr;
    CountMidiChannels m_countMidiChannels = nullptr;
    static constexpr int kMetronomeChannel = 16;
};

} // namespace midi_play::audio
