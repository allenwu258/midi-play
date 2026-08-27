#pragma once

#include "domain/playback/playbacktypes.h"

#include <QLibrary>
#include <QMap>
#include <QObject>
#include <QString>

struct fluid_settings_t;
struct fluid_synth_t;
struct fluid_audio_driver_t;
struct fluid_sequencer_t;
struct fluid_event_t;

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
    bool setTransportPosition(qint64 microseconds);
    qint64 clockPositionUs() const;
    bool supportsTimedEvents() const;
    bool flush();
    void submit(const playback::PlaybackEvent& event);
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
    fluid_sequencer_t* m_sequencer = nullptr;
    short m_sequencerDestination = -1;
    unsigned int m_transportTick = 0;
    unsigned int m_transportTickAtSet = 0;
    qint64 m_transportPositionUs = 0;
    bool m_useTimedSequencer = false;
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
    using NewSequencer = fluid_sequencer_t* (*)(int);
    using DeleteSequencer = void (*)(fluid_sequencer_t*);
    using RegisterSynth = short (*)(fluid_sequencer_t*, fluid_synth_t*);
    using NewEvent = fluid_event_t* (*)();
    using DeleteEvent = void (*)(fluid_event_t*);
    using EventSetDest = void (*)(fluid_event_t*, short);
    using EventNoteOn = void (*)(fluid_event_t*, int, short, short);
    using EventNoteOff = void (*)(fluid_event_t*, int, short);
    using EventAllNotesOff = void (*)(fluid_event_t*, int);
    using EventProgramSelect = void (*)(fluid_event_t*, int, unsigned int, short, short);
    using EventControlChange = void (*)(fluid_event_t*, int, short, int);
    using EventPitchBend = void (*)(fluid_event_t*, int, int);
    using EventChannelPressure = void (*)(fluid_event_t*, int, int);
    using SequencerSendAt = int (*)(fluid_sequencer_t*, fluid_event_t*, unsigned int, int);
    using SequencerGetTick = unsigned int (*)(fluid_sequencer_t*);
    using SequencerRemoveEvents = void (*)(fluid_sequencer_t*, short, short, int);

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
    NewSequencer m_newSequencer = nullptr;
    DeleteSequencer m_deleteSequencer = nullptr;
    RegisterSynth m_registerSynth = nullptr;
    NewEvent m_newEvent = nullptr;
    DeleteEvent m_deleteEvent = nullptr;
    EventSetDest m_eventSetDest = nullptr;
    EventNoteOn m_eventNoteOn = nullptr;
    EventNoteOff m_eventNoteOff = nullptr;
    EventAllNotesOff m_eventAllNotesOff = nullptr;
    EventProgramSelect m_eventProgramSelect = nullptr;
    EventControlChange m_eventControlChange = nullptr;
    EventPitchBend m_eventPitchBend = nullptr;
    EventChannelPressure m_eventChannelPressure = nullptr;
    SequencerSendAt m_sequencerSendAt = nullptr;
    SequencerGetTick m_sequencerGetTick = nullptr;
    SequencerRemoveEvents m_sequencerRemoveEvents = nullptr;
};

} // namespace midi_play::audio
