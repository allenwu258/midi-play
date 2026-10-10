#include "fluidsynthengine.h"

// 实现动态符号加载、合成器生命周期、音源事务及实时/离线 PCM 输出。
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

// 捕获列表按线程隔离；安装日志回调和加载音源则由进程级互斥量串行化。
thread_local QStringList* s_activeFluidLogs = nullptr;
std::mutex s_fluidLoadMutex;

// FluidSynth 日志回调；只有处于音源加载捕获范围的线程才记录诊断。
void fluidLogHandler(int level, const char* message, void*)
{
    const QString text = QString::fromUtf8(message ? message : "").trimmed();
    if (text.isEmpty()) return;
    // 实时音频线程没有此 TLS 捕获列表，不向 Qt 日志输出消息。
    // 加载线程收集警告以上消息，严重错误同时写入应用日志。
    if (s_activeFluidLogs) {
        if (level <= 2) s_activeFluidLogs->push_back(text);
        if (level <= 1) qWarning().noquote() << "FluidSynth:" << text;
    }
}

// RAII 管理当前线程的捕获目标，允许嵌套调用并在退出时恢复上一目标。
class ScopedFluidLogCapture final {
public:
    // 安装本次音源加载的诊断容器。
    explicit ScopedFluidLogCapture(QStringList* logs)
        : m_previous(s_activeFluidLogs)
    {
        s_activeFluidLogs = logs;
    }

    // 无论加载成功或失败，都恢复原 TLS 状态。
    ~ScopedFluidLogCapture()
    {
        s_activeFluidLogs = m_previous;
    }

private:
    QStringList* m_previous = nullptr;
};

// 去重并保留最多三条诊断，控制用户错误信息的长度。
QString conciseFluidLogs(const QStringList& logs)
{
    QStringList unique;
    for (const QString& message : logs) {
        if (!unique.contains(message)) unique.push_back(message);
        if (unique.size() == 3) break;
    }
    return unique.join(QStringLiteral("；"));
}

// 仅识别明确的 SF3 未编译/不可用日志，避免把坏文件误判为运行时能力缺失。
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

// 为 SF3 缺少解码能力提供针对性诊断，其他失败组合路径与精简库日志。
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

// 延迟初始化外部资源，使对象创建不依赖动态库或音频设备可用。
FluidSynthEngine::FluidSynthEngine(QObject* parent)
    : QObject(parent)
{
}

// 先停止原生回调，再释放其访问的资源。
FluidSynthEngine::~FluidSynthEngine()
{
    release();
}

// 加载平台候选库，解析函数表；缺少必需符号时卸载库并报告失败。
bool FluidSynthEngine::resolveSymbols(QString* error)
{
    if (m_library.isLoaded()) return true;

#ifdef Q_OS_WIN
    // 优先使用发行目录中的库；该文件存在却加载失败时不切换到系统中的其他版本。
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

    // 通过同一动态库解析 C ABI；可选控制和节拍器符号允许为空。
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

    // 先确认合成、音源及核心 MIDI API；实时驱动 API 在 load 路径另行检查。
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

// 清理旧资源后建立新合成器，失败路径按统一 release 顺序回收已创建部分。
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
        // 播放按需解码以控制启动耗时和内存；独立验证预加载全部采样，
        // 提前发现编码器缺失或采样数据损坏。
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

// 尽力创建独立点击声部池；缺少可选符号或配置失败不影响歌曲合成器。
void FluidSynthEngine::initializeMetronomeSynth()
{
    // 使用独立合成器保持歌曲复音数；两个合成器采样率必须一致才能直接混音。
    if (!m_process || !m_settingsSetInt || !m_settingsSetNum
        || !m_settingsGetNum || !m_setChannelType || !m_allSoundsOff || !m_cc) return;
    double sampleRate = 0;
    if (m_settingsGetNum(m_settings, "synth.sample-rate", &sampleRate) != 0) return;
    m_metronomeSettings = m_newSettings();
    if (!m_metronomeSettings) return;
    // 点击不用歌曲的混响/合唱，独立的小型声部池避免密集歌曲声部挤占节拍器。
    const bool configured = m_settingsSetNum(m_metronomeSettings, "synth.sample-rate", sampleRate) == 0
        && m_settingsSetNum(m_metronomeSettings, "synth.gain", 0.8) == 0
        && m_settingsSetInt(m_metronomeSettings, "synth.polyphony", 16) == 0
        && m_settingsSetInt(m_metronomeSettings, "synth.reverb.active", 0) == 0
        && m_settingsSetInt(m_metronomeSettings, "synth.chorus.active", 0) == 0
        && m_settingsSetInt(m_metronomeSettings, "synth.dynamic-sample-loading", 0) == 0;
    if (configured) m_metronomeSynth = m_newSynth(m_metronomeSettings);
    if (!m_metronomeSynth) releaseMetronomeSynth();
}

// 原生驱动回调，只进行输出路由和合成，不分配中间缓冲区或等待控制线程。
int FluidSynthEngine::renderAudio(void* context, int frames, int effectCount,
                                  float** effects, int outputCount, float** outputs) noexcept
{
    auto& engine = *static_cast<FluidSynthEngine*>(context);
    // 驱动提供清零的输出，process 累加采样，两次处理即可直接混合歌曲与点击。
    // 驱动没有独立效果输出时，将歌曲混响/合唱路由到主输出。
    // 数组只保存驱动缓冲区地址；效果输出与主输出可别名，process 按累加语义混入歌曲效果。
    float* mixedEffects[2] {};
    if (effectCount == 0 && outputCount >= 2) {
        mixedEffects[0] = outputs[0];
        mixedEffects[1] = outputs[1];
        effects = mixedEffects;
        effectCount = 2;
    }
    const int songResult = engine.m_process(engine.m_synth, frames, effectCount, effects,
                                             outputCount, outputs);
    // 点击合成器无效果，仅向首组立体声输出累加采样。
    const int clickResult = engine.m_process(engine.m_metronomeSynth, frames, 0, nullptr,
                                              std::min(outputCount, 2), outputs);
    return songResult == 0 && clickResult == 0 ? 0 : -1;
}

// 在无设备模式下完整解码音源；检查结束即释放，不保留验证用合成器。
bool FluidSynthEngine::validateSoundFont(const QString& soundFontPath, QString* error)
{
    if (error) error->clear();
    const bool valid = initializeSynth(soundFontPath, error, false);
    release();
    return valid;
}

// 首次加载创建实时输出；已有合成器则采用候选音源先加载的切换路径。
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

    // 原生驱动拥有实时音频线程。混音驱动失败时释放点击合成器并回退歌曲驱动。
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

// 离线模式只创建合成资源；若明确要求节拍器，准备失败则整个初始化失败。
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

// 调用方每次拉取一个采样块；使用相同输出缓冲区合并歌曲效果和点击。
bool FluidSynthEngine::renderOffline(int frames, float* left, float* right, QString* error)
{
    if (!m_loaded || !m_synth || frames <= 0 || !left || !right) {
        if (error) *error = QStringLiteral("离线音频引擎尚未准备好");
        return false;
    }
    // process 采用累加语义，必须先清零，避免前一块 PCM 污染本块。
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

// 按值返回探测结果，调用方不持有内部状态的可写引用。
FluidSynthCapabilities FluidSynthEngine::capabilities() const
{
    return m_capabilities;
}

// 在同一合成器与驱动上切换音源，失败时仍可继续使用旧 SoundFont。
bool FluidSynthEngine::loadSoundFontIntoActiveSynth(const QString& soundFontPath, QString* error)
{
    if (!m_synth || !m_sfload) {
        if (error) *error = QStringLiteral("音频引擎尚未初始化");
        return false;
    }

    // 候选加载不重置音色，也不销毁旧资源；加载成功后才更新活动音源 ID。
    int candidateId = -1;
    if (!loadIntoSynth(m_synth, soundFontPath, 0, &candidateId, error)) {
        return false;
    }

    // 活动 ID 更新后再卸载旧音源，后续 programSelect 始终引用已成功加载的候选资源。
    const int previousId = m_soundFontId;
    m_soundFontId = candidateId;
    if (previousId >= 0 && m_sfunload && m_sfunload(m_synth, previousId, 0) != 0) {
        // 新音源已经可用；旧音源卸载失败时仍保留切换结果，残留资源在析构时释放。
        if (error) error->clear();
    }
    return true;
}

// 容器预检通过后执行真正的 FluidSynth 加载，记录格式能力和面向用户的错误。
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

    // 日志回调是 FluidSynth 进程级状态；安装回调与 sfload 必须串行执行，
    // 防止并发验证在 TLS 捕获期间竞争全局回调。
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

// SF3 加载成功可确认支持；只有明确的缺失能力日志才能确认不支持。
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

// 配置初始 bank 0 音色；运行中的 bank/program 变化由事件分派处理。
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

// 即时后端的驱动持续运行；传输开始/暂停只检查就绪，停止/跳转则清理合成状态。
bool FluidSynthEngine::start() { return m_loaded; }
bool FluidSynthEngine::pause() { return m_loaded; }
bool FluidSynthEngine::stop() { return flush(); }
bool FluidSynthEngine::seek(qint64) { return flush(); }
// 内部没有时间事件队列，无需保存会话的传输位置。
bool FluidSynthEngine::setTransportPosition(qint64) { return m_loaded; }
// 不提供音频设备位置或定时事件契约，播放会话负责软件时钟调度。
qint64 FluidSynthEngine::clockPositionUs() const { return -1; }
bool FluidSynthEngine::supportsTimedEvents() const { return false; }
// 与服务层的静态能力声明不同，此处返回资源的实际准备状态。
bool FluidSynthEngine::supportsMetronome() const
{
    return m_loaded && m_metronomeReady;
}

// Qt 资源需提取成文件供 sfload 使用；临时文件与点击合成器共同存活。
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
        // 小型点击采样在非实时路径预加载，音源文件保留到合成器释放之后。
        if (!loadIntoSynth(m_metronomeSynth, path, 0, &m_metronomeSoundFontId, error)) return false;
        m_metronomeFile = std::move(file);
    }
    m_metronomeReady = configureMetronomeChannel();
    if (!m_metronomeReady && error) *error = QStringLiteral("无法配置独立节拍器音色");
    return m_metronomeReady;
}

// 初始化点击专用通道，使用户音源、踏板和歌曲效果不会改变节拍器音色。
bool FluidSynthEngine::configureMetronomeChannel()
{
    if (!m_metronomeSynth || m_metronomeSoundFontId < 0) return false;
    // bank 128/program 127 仅存在于内置音源，必须按其 SoundFont ID 显式选择。
    if (m_setChannelType(m_metronomeSynth, kMetronomeChannel, 1) != 0
        || m_programSelect(m_metronomeSynth, kMetronomeChannel, m_metronomeSoundFontId, 128, 127) != 0) return false;
    // 清除两种踏板及混响/合唱发送量，使点击衰减只由内置短采样决定。
    for (int controller : {64, 66, 91, 93}) {
        if (m_cc(m_metronomeSynth, kMetronomeChannel, controller, 0) != 0) return false;
    }
    return m_cc(m_metronomeSynth, kMetronomeChannel, 7, 100) == 0
        && m_cc(m_metronomeSynth, kMetronomeChannel, 11, 127) == 0;
}

// 强拍与弱拍分别使用内置音高 60/61，在独立声部池触发。
void FluidSynthEngine::submitMetronomeClick(bool accent)
{
    if (!supportsMetronome()) return;
    // 无循环的 60 ms 采样自行结束，无需创建延迟 NoteOff 任务。
    m_noteOn(m_metronomeSynth, kMetronomeChannel, accent ? 60 : 61, 100);
}

// 强制结束所有点击声部，包括仍处于采样播放中的点击。
void FluidSynthEngine::stopMetronome()
{
    if (m_metronomeSynth && m_allSoundsOff)
        m_allSoundsOff(m_metronomeSynth, kMetronomeChannel);
}

// 同时清理点击和歌曲状态；歌曲使用 system_reset 以便后续精确重建控制器。
bool FluidSynthEngine::flush()
{
    if (!m_loaded) return false;
    stopMetronome();
    const bool reset = m_systemReset && m_systemReset(m_synth) == 0;
    return reset;
}

// 将统一播放事件翻译为即时 API 调用，不根据事件时间戳自行等待。
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
        // 以最接近的通道级 MIDI 控制近似单音符表达；不改写领域事件本身。
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

// 音符触发由会话保证顺序；这里只映射通道并检查后端是否加载。
void FluidSynthEngine::noteOn(int channel, int pitch, int velocity)
{
    if (m_loaded && m_noteOn) m_noteOn(m_synth, channel % 16, pitch, velocity);
}

// 显式释放音符，踏板及尾音由 FluidSynth 当前通道状态决定。
void FluidSynthEngine::noteOff(int channel, int pitch)
{
    if (m_loaded && m_noteOff) m_noteOff(m_synth, channel % 16, pitch);
}

// 组合 14 位 bank 编号，始终在当前活动 SoundFont 中查找音色。
void FluidSynthEngine::programChange(int channel, int program, int bankMsb, int bankLsb)
{
    if (m_loaded && m_programSelect) {
        const unsigned int bank = static_cast<unsigned int>((std::clamp(bankMsb, 0, 127) << 7)
                                                            | std::clamp(bankLsb, 0, 127));
        m_programSelect(m_synth, channel % 16, m_soundFontId, bank, program % 128);
    }
}

// 可选控制器 API 缺失时忽略消息，保留基础音符播放能力。
void FluidSynthEngine::controlChange(int channel, int controller, int value)
{
    if (m_loaded && m_cc) m_cc(m_synth, channel % 16, controller % 128, value % 128);
}

// 弯音值限制在 14 位范围，8192 表示中心位置。
void FluidSynthEngine::pitchBend(int channel, int value)
{
    if (m_loaded && m_pitchBend) m_pitchBend(m_synth, channel % 16, std::clamp(value, 0, 16383));
}

// 通道压力使用 7 位值，缺少可选符号时跳过。
void FluidSynthEngine::channelPressure(int channel, int value)
{
    if (m_loaded && m_channelPressure) m_channelPressure(m_synth, channel % 16, std::clamp(value, 0, 127));
}

// 按键压力保留目标音高，音高和值都限制到 MIDI 范围。
void FluidSynthEngine::polyPressure(int channel, int pitch, int value)
{
    if (m_loaded && m_keyPressure) {
        m_keyPressure(m_synth, channel % 16, std::clamp(pitch, 0, 127), std::clamp(value, 0, 127));
    }
}

// 先销毁驱动并等待原生回调结束，再释放歌曲、点击及它们依赖的设置。
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

// 驱动不再访问点击对象后才可调用；先销毁合成器，最后删除临时音源。
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
