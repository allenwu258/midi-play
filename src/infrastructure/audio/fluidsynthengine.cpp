#include "fluidsynthengine.h"

#include <QtGlobal>
#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QStringList>

#include <algorithm>
#include <mutex>

namespace midi_play::audio {
namespace {

thread_local QStringList* s_activeFluidLogs = nullptr;
std::mutex s_fluidLoadMutex;

void fluidLogHandler(int level, const char* message, void*)
{
    const QString text = QString::fromUtf8(message ? message : "").trimmed();
    if (text.isEmpty()) return;
    // Do not log from FluidSynth's realtime audio thread. During a guarded
    // load operation the caller owns the TLS capture and receives diagnostics
    // in its error result instead.
    if (s_activeFluidLogs) {
        if (level <= 2) s_activeFluidLogs->push_back(text);
        if (level <= 1) qWarning().noquote() << "FluidSynth:" << text;
    }
}

class ScopedFluidLogCapture final {
public:
    explicit ScopedFluidLogCapture(QStringList* logs)
        : m_previous(s_activeFluidLogs)
    {
        s_activeFluidLogs = logs;
    }

    ~ScopedFluidLogCapture()
    {
        s_activeFluidLogs = m_previous;
    }

private:
    QStringList* m_previous = nullptr;
};

QString conciseFluidLogs(const QStringList& logs)
{
    QStringList unique;
    for (const QString& message : logs) {
        if (!unique.contains(message)) unique.push_back(message);
        if (unique.size() == 3) break;
    }
    return unique.join(QStringLiteral("；"));
}

bool logsIndicateMissingSf3Support(const QStringList& logs)
{
    for (const QString& message : logs) {
        const QString lower = message.toLower();
        if ((lower.contains(QStringLiteral("compiled without support"))
             && lower.contains(QStringLiteral("v3")))
            || lower.contains(QStringLiteral("sf3 support is not available"))) {
            return true;
        }
    }
    return false;
}

QString soundFontLoadError(const QString& path, SoundFontFormat format,
                           const QStringList& logs)
{
    if (format == SoundFontFormat::Sf3 && logsIndicateMissingSf3Support(logs)) {
        return QStringLiteral("当前 FluidSynth 运行时未启用 SF3 支持。请安装包含 "
                              "libsndfile/Ogg Vorbis 支持的完整音频运行时: %1")
            .arg(path);
    }

    const QString detail = conciseFluidLogs(logs);
    const QString formatName = format == SoundFontFormat::Sf3
        ? QStringLiteral("SF3") : QStringLiteral("SoundFont");
    return detail.isEmpty()
        ? QStringLiteral("无法解析 %1 音源: %2").arg(formatName, path)
        : QStringLiteral("无法解析 %1 音源: %2。FluidSynth: %3")
              .arg(formatName, path, detail);
}

} // namespace

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
    const QString deployedLibrary = QDir(QCoreApplication::applicationDirPath())
        .filePath(QStringLiteral("libfluidsynth-3.dll"));
    const bool deployedLibraryExists = QFileInfo::exists(deployedLibrary);
    const QStringList candidates = deployedLibraryExists
        ? QStringList { deployedLibrary }
        : QStringList { QStringLiteral("libfluidsynth-3"), QStringLiteral("fluidsynth") };
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
#ifdef Q_OS_WIN
            *error = deployedLibraryExists
                ? QStringLiteral("FluidSynth 动态库存在，但无法加载。发行目录可能缺少 "
                                 "libsndfile/Ogg Vorbis 等传递依赖。详情: %1")
                      .arg(m_library.errorString())
                : QStringLiteral("未找到 FluidSynth 动态库。请确认 libfluidsynth-3.dll "
                                 "位于程序目录。详情: %1").arg(m_library.errorString());
#else
            *error = QStringLiteral("未找到 FluidSynth 动态库。请安装 fluidsynth 并确保其位于系统库路径。详情: %1")
                         .arg(m_library.errorString());
#endif
        }
        return false;
    }

    auto resolve = [this](const char* name) { return m_library.resolve(name); };
    m_newSettings = reinterpret_cast<NewSettings>(resolve("new_fluid_settings"));
    m_deleteSettings = reinterpret_cast<DeleteSettings>(resolve("delete_fluid_settings"));
    m_settingsSetNum = reinterpret_cast<SettingsSetNum>(resolve("fluid_settings_setnum"));
    m_settingsGetNum = reinterpret_cast<SettingsGetNum>(resolve("fluid_settings_getnum"));
    m_settingsSetInt = reinterpret_cast<SettingsSetInt>(resolve("fluid_settings_setint"));
    m_settingsSetStr = reinterpret_cast<SettingsSetStr>(resolve("fluid_settings_setstr"));
    m_setLogFunction = reinterpret_cast<SetLogFunction>(resolve("fluid_set_log_function"));
    m_versionString = reinterpret_cast<VersionString>(resolve("fluid_version_str"));
    m_newSynth = reinterpret_cast<NewSynth>(resolve("new_fluid_synth"));
    m_deleteSynth = reinterpret_cast<DeleteSynth>(resolve("delete_fluid_synth"));
    m_newAudioDriver = reinterpret_cast<NewAudioDriver>(resolve("new_fluid_audio_driver"));
    m_newAudioDriver2 = reinterpret_cast<NewAudioDriver2>(resolve("new_fluid_audio_driver2"));
    m_process = reinterpret_cast<Process>(resolve("fluid_synth_process"));
    m_deleteAudioDriver = reinterpret_cast<DeleteAudioDriver>(resolve("delete_fluid_audio_driver"));
    m_sfload = reinterpret_cast<Sfload>(resolve("fluid_synth_sfload"));
    m_sfunload = reinterpret_cast<Sfunload>(resolve("fluid_synth_sfunload"));
    m_programSelect = reinterpret_cast<ProgramSelect>(resolve("fluid_synth_program_select"));
    m_noteOn = reinterpret_cast<NoteOn>(resolve("fluid_synth_noteon"));
    m_noteOff = reinterpret_cast<NoteOff>(resolve("fluid_synth_noteoff"));
    m_cc = reinterpret_cast<Cc>(resolve("fluid_synth_cc"));
    m_pitchBend = reinterpret_cast<PitchBend>(resolve("fluid_synth_pitch_bend"));
    m_channelPressure = reinterpret_cast<ChannelPressure>(resolve("fluid_synth_channel_pressure"));
    m_keyPressure = reinterpret_cast<KeyPressure>(resolve("fluid_synth_key_pressure"));
    m_systemReset = reinterpret_cast<SystemReset>(resolve("fluid_synth_system_reset"));
    m_setChannelType = reinterpret_cast<SetChannelType>(resolve("fluid_synth_set_channel_type"));
    m_allSoundsOff = reinterpret_cast<AllSoundsOff>(resolve("fluid_synth_all_sounds_off"));

    if (!m_newSettings || !m_deleteSettings || !m_newSynth || !m_deleteSynth || !m_sfload
        || !m_programSelect || !m_noteOn || !m_noteOff || !m_systemReset
        || !m_settingsSetNum || !m_process) {
        if (error) *error = QStringLiteral("FluidSynth 动态库缺少必要 API");
        m_library.unload();
        return false;
    }

    if (m_versionString) {
        const char* version = m_versionString();
        m_capabilities.version = QString::fromLatin1(version ? version : "");
    }
    return true;
}

bool FluidSynthEngine::initializeSynth(const QString& soundFontPath, QString* error,
                                       bool dynamicSampleLoading, int sampleRate, bool metronome)
{
    release();
    if (!resolveSymbols(error)) return false;

    m_settings = m_newSettings();
    if (!m_settings) {
        if (error) *error = QStringLiteral("无法创建 FluidSynth settings");
        return false;
    }
    if (m_settingsSetNum) m_settingsSetNum(m_settings, "synth.gain", 0.8);
    if (sampleRate > 0 && m_settingsSetNum(m_settings, "synth.sample-rate", sampleRate) != 0) {
        if (error) *error = QStringLiteral("无法设置 FluidSynth 采样率");
        release();
        return false;
    }
    if (m_settingsSetInt) {
        // Playback uses on-demand decoding to bound startup time and memory.
        // Standalone validation passes false so FluidSynth eagerly decodes all
        // samples and catches codec/data errors before the user presses Play.
        m_settingsSetInt(m_settings, "synth.dynamic-sample-loading",
                         dynamicSampleLoading ? 1 : 0);
    }

    m_synth = m_newSynth(m_settings);
    if (!m_synth) {
        if (error) *error = QStringLiteral("无法创建 FluidSynth synthesizer");
        release();
        return false;
    }
    if (!loadIntoSynth(m_synth, soundFontPath, 1, &m_soundFontId, error)) {
        release();
        return false;
    }
    if (dynamicSampleLoading && metronome) initializeMetronomeSynth();
    return true;
}

void FluidSynthEngine::initializeMetronomeSynth()
{
    // An optional feature must never reduce song polyphony or prevent playback.
    if (!m_process || !m_settingsSetInt || !m_settingsSetNum
        || !m_settingsGetNum || !m_setChannelType || !m_allSoundsOff || !m_cc) return;
    double sampleRate = 0;
    if (m_settingsGetNum(m_settings, "synth.sample-rate", &sampleRate) != 0) return;
    m_metronomeSettings = m_newSettings();
    if (!m_metronomeSettings) return;
    const bool configured = m_settingsSetNum(m_metronomeSettings, "synth.sample-rate", sampleRate) == 0
        && m_settingsSetNum(m_metronomeSettings, "synth.gain", 0.8) == 0
        && m_settingsSetInt(m_metronomeSettings, "synth.polyphony", 16) == 0
        && m_settingsSetInt(m_metronomeSettings, "synth.reverb.active", 0) == 0
        && m_settingsSetInt(m_metronomeSettings, "synth.chorus.active", 0) == 0
        && m_settingsSetInt(m_metronomeSettings, "synth.dynamic-sample-loading", 0) == 0;
    if (configured) m_metronomeSynth = m_newSynth(m_metronomeSettings);
    if (!m_metronomeSynth) releaseMetronomeSynth();
}

int FluidSynthEngine::renderAudio(void* context, int frames, int effectCount,
                                  float** effects, int outputCount, float** outputs) noexcept
{
    auto& engine = *static_cast<FluidSynthEngine*>(context);
    // FluidSynth drivers supply zeroed buffers. process() ADDS samples, so two
    // synths need no intermediate buffers, allocation, locks or Qt calls here.
    // Preserve song reverb/chorus on drivers with no separate effect outputs.
    float* mixedEffects[2] {};
    if (effectCount == 0 && outputCount >= 2) {
        mixedEffects[0] = outputs[0];
        mixedEffects[1] = outputs[1];
        effects = mixedEffects;
        effectCount = 2;
    }
    const int songResult = engine.m_process(engine.m_synth, frames, effectCount, effects,
                                             outputCount, outputs);
    // The click synth has no effects and a single stereo output group.
    const int clickResult = engine.m_process(engine.m_metronomeSynth, frames, 0, nullptr,
                                              std::min(outputCount, 2), outputs);
    return songResult == 0 && clickResult == 0 ? 0 : -1;
}

bool FluidSynthEngine::validateSoundFont(const QString& soundFontPath, QString* error)
{
    if (error) error->clear();
    const bool valid = initializeSynth(soundFontPath, error, false);
    release();
    return valid;
}

bool FluidSynthEngine::load(const QString& soundFontPath, QString* error)
{
    if (error) error->clear();
    if (m_loaded) {
        return loadSoundFontIntoActiveSynth(soundFontPath, error);
    }
    if (!initializeSynth(soundFontPath, error)) return false;
    if (!m_newAudioDriver || !m_deleteAudioDriver) {
        if (error) *error = QStringLiteral("FluidSynth 动态库缺少音频驱动 API");
        release();
        return false;
    }

    // FluidSynth owns the realtime audio thread through its native driver.
    if (m_metronomeSynth) {
        m_driver = m_newAudioDriver2(m_settings, &FluidSynthEngine::renderAudio, this);
        if (!m_driver) releaseMetronomeSynth();
    }
    if (!m_driver) m_driver = m_newAudioDriver(m_settings, m_synth);
    if (!m_driver) {
        if (error) *error = QStringLiteral("无法创建 FluidSynth 音频驱动");
        release();
        return false;
    }
    m_loaded = true;
    return true;
}

bool FluidSynthEngine::loadOffline(const QString& soundFontPath, int sampleRate,
                                   bool metronome, QString* error)
{
    if (error) error->clear();
    if (sampleRate != 44100 && sampleRate != 48000) {
        if (error) *error = QStringLiteral("导出采样率必须为 44100 或 48000 Hz");
        return false;
    }
    if (!initializeSynth(soundFontPath, error, true, sampleRate, metronome)) return false;
    m_loaded = true;
    if (metronome && !prepareMetronome(error)) {
        release();
        return false;
    }
    return true;
}

bool FluidSynthEngine::renderOffline(int frames, float* left, float* right, QString* error)
{
    if (!m_loaded || !m_synth || frames <= 0 || !left || !right) {
        if (error) *error = QStringLiteral("离线音频引擎尚未准备好");
        return false;
    }
    std::fill_n(left, frames, 0.0f);
    std::fill_n(right, frames, 0.0f);
    float* outputs[2] {left, right};
    if (m_process(m_synth, frames, 2, outputs, 2, outputs) != 0
        || (m_metronomeReady && m_process(m_metronomeSynth, frames, 0, nullptr, 2, outputs) != 0)) {
        if (error) *error = QStringLiteral("FluidSynth 离线渲染失败");
        return false;
    }
    return true;
}

FluidSynthCapabilities FluidSynthEngine::capabilities() const
{
    return m_capabilities;
}

bool FluidSynthEngine::loadSoundFontIntoActiveSynth(const QString& soundFontPath, QString* error)
{
    if (!m_synth || !m_sfload) {
        if (error) *error = QStringLiteral("音频引擎尚未初始化");
        return false;
    }

    // Keep the active synth and audio driver alive while FluidSynth parses the
    // replacement. A malformed file therefore cannot silence the previous
    // source or force the audio device through a second open/close cycle.
    int candidateId = -1;
    if (!loadIntoSynth(m_synth, soundFontPath, 0, &candidateId, error)) {
        return false;
    }

    const int previousId = m_soundFontId;
    m_soundFontId = candidateId;
    if (previousId >= 0 && m_sfunload && m_sfunload(m_synth, previousId, 0) != 0) {
        // The new source is usable. Retaining an old source is safer than
        // undoing a successful switch; FluidSynth will release it on teardown.
        if (error) error->clear();
    }
    return true;
}

bool FluidSynthEngine::loadIntoSynth(fluid_synth_t* synth, const QString& soundFontPath, int resetPresets,
                                     int* soundFontId, QString* error)
{
    QString inspectionError;
    const SoundFontInspection inspection = SoundFontInspector::inspect(soundFontPath,
                                                                        &inspectionError);
    if (!inspection.validRiffContainer) {
        if (error) *error = inspectionError;
        return false;
    }

    // fluid_set_log_function is process-global in FluidSynth. Serialize
    // callback installation and sfload so concurrent validators cannot race
    // on the global callback while their thread-local captures are active.
    const std::lock_guard<std::mutex> loadGuard(s_fluidLoadMutex);
    if (m_setLogFunction) {
        for (int level = 0; level <= 4; ++level) {
            m_setLogFunction(level, fluidLogHandler, nullptr);
        }
    }

    QStringList logs;
    ScopedFluidLogCapture capture(&logs);
    const QByteArray encodedPath = soundFontPath.toUtf8();
    const int id = m_sfload(synth, encodedPath.constData(), resetPresets);
    updateFormatCapability(inspection.format, id >= 0, logs);
    if (id < 0) {
        if (error) *error = soundFontLoadError(soundFontPath, inspection.format, logs);
        return false;
    }
    if (soundFontId) *soundFontId = id;
    return true;
}

void FluidSynthEngine::updateFormatCapability(SoundFontFormat format, bool loaded,
                                               const QStringList& logs)
{
    if (format != SoundFontFormat::Sf3) return;
    if (loaded) {
        m_capabilities.supportsSf3 = BackendFeatureSupport::Supported;
    } else if (logsIndicateMissingSf3Support(logs)) {
        m_capabilities.supportsSf3 = BackendFeatureSupport::Unsupported;
    }
}

bool FluidSynthEngine::configureTrack(int channel, int program, QString* error)
{
    if (!m_loaded || !m_programSelect) {
        if (error) *error = QStringLiteral("音频引擎尚未加载 SoundFont");
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
bool FluidSynthEngine::supportsMetronome() const
{
    return m_loaded && m_metronomeReady;
}

bool FluidSynthEngine::prepareMetronome(QString* error)
{
    if (error) error->clear();
    if (!m_loaded || !m_metronomeSynth) {
        if (error) *error = QStringLiteral("当前音频引擎无法提供独立的节拍器合成与混音输出");
        return false;
    }
    if (m_metronomeSoundFontId < 0) {
        QFile resource(QStringLiteral(":/midi_play/audio/metronome.sf2"));
        auto file = std::make_unique<QTemporaryFile>(
            QDir::tempPath() + QStringLiteral("/midi-play-click-XXXXXX.sf2"));
        if (!resource.open(QIODevice::ReadOnly) || !file->open()) {
            if (error) *error = QStringLiteral("无法读取或提取内置节拍器音源");
            return false;
        }
        const auto data = resource.readAll();
        if (data.isEmpty() || file->write(data) != data.size() || !file->flush()) {
            if (error) *error = QStringLiteral("无法写入节拍器临时音源");
            return false;
        }
        const QString path = file->fileName();
        file->close();
        // The click synth eagerly loads these tiny samples off the audio thread.
        // Keep its backing file alive until the synth is released.
        if (!loadIntoSynth(m_metronomeSynth, path, 0, &m_metronomeSoundFontId, error)) return false;
        m_metronomeFile = std::move(file);
    }
    m_metronomeReady = configureMetronomeChannel();
    if (!m_metronomeReady && error) *error = QStringLiteral("无法配置独立节拍器音色");
    return m_metronomeReady;
}

bool FluidSynthEngine::configureMetronomeChannel()
{
    if (!m_metronomeSynth || m_metronomeSoundFontId < 0) return false;
    // This bank/preset exists only in the bundled font. Always select by font
    // ID, bypassing the ordinary channel % 16 path and user SoundFont fallback.
    if (m_setChannelType(m_metronomeSynth, kMetronomeChannel, 1) != 0
        || m_programSelect(m_metronomeSynth, kMetronomeChannel, m_metronomeSoundFontId, 128, 127) != 0) return false;
    for (int controller : {64, 66, 91, 93}) {
        if (m_cc(m_metronomeSynth, kMetronomeChannel, controller, 0) != 0) return false;
    }
    return m_cc(m_metronomeSynth, kMetronomeChannel, 7, 100) == 0
        && m_cc(m_metronomeSynth, kMetronomeChannel, 11, 127) == 0;
}

void FluidSynthEngine::submitMetronomeClick(bool accent)
{
    if (!supportsMetronome()) return;
    // Finite, non-looping 60 ms samples need no delayed note-off task.
    m_noteOn(m_metronomeSynth, kMetronomeChannel, accent ? 60 : 61, 100);
}

void FluidSynthEngine::stopMetronome()
{
    if (m_metronomeSynth && m_allSoundsOff)
        m_allSoundsOff(m_metronomeSynth, kMetronomeChannel);
}

bool FluidSynthEngine::flush()
{
    if (!m_loaded) return false;
    stopMetronome();
    const bool reset = m_systemReset && m_systemReset(m_synth) == 0;
    return reset;
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
    case playback::PlaybackEventKind::PolyPressure:
        polyPressure(event.channel, event.pitch, event.value);
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

void FluidSynthEngine::polyPressure(int channel, int pitch, int value)
{
    if (m_loaded && m_keyPressure) {
        m_keyPressure(m_synth, channel % 16, std::clamp(pitch, 0, 127), std::clamp(value, 0, 127));
    }
}

void FluidSynthEngine::release()
{
    m_loaded = false;
    if (m_driver && m_deleteAudioDriver) m_deleteAudioDriver(m_driver);
    m_driver = nullptr;
    if (m_synth && m_deleteSynth) m_deleteSynth(m_synth);
    m_synth = nullptr;
    releaseMetronomeSynth();
    if (m_settings && m_deleteSettings) m_deleteSettings(m_settings);
    m_settings = nullptr;
    m_soundFontId = -1;
}

void FluidSynthEngine::releaseMetronomeSynth()
{
    if (m_metronomeSynth && m_deleteSynth) m_deleteSynth(m_metronomeSynth);
    m_metronomeSynth = nullptr;
    if (m_metronomeSettings && m_deleteSettings) m_deleteSettings(m_metronomeSettings);
    m_metronomeSettings = nullptr;
    m_metronomeReady = false;
    m_metronomeSoundFontId = -1;
    m_metronomeFile.reset();
}

} // namespace midi_play::audio
