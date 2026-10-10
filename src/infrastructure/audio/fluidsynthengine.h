#pragma once

// FluidSynth 动态库封装：音源验证、实时/离线合成、MIDI 分派及独立节拍器。
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

// 三态能力标记：未知不等于不支持，必须由实际加载结果确认。
enum class BackendFeatureSupport {
    Unknown,
    Unsupported,
    Supported
};

// 运行时版本和音源格式能力；SF3 支持由加载结果及库日志逐步确定。
struct FluidSynthCapabilities {
    QString version;
    bool supportsSf2 = true;
    BackendFeatureSupport supportsSf3 = BackendFeatureSupport::Unknown;
};

// 持有动态符号、合成器、驱动及音源资源；控制调用由上层串行化。
// 实时 PCM 回调由 FluidSynth 驱动线程执行，离线模式由调用方主动拉取采样。
class FluidSynthEngine final : public QObject {
    Q_OBJECT
public:
    // 仅创建封装对象，动态库和音频设备在后续加载时初始化。
    explicit FluidSynthEngine(QObject* parent = nullptr);
    // 按驱动、合成器、设置的依赖顺序释放资源。
    ~FluidSynthEngine() override;

    // 不打开设备，完整加载采样以验证文件，再释放本对象的合成资源。
    bool validateSoundFont(const QString& soundFontPath, QString* error);
    // 首次加载创建实时驱动；已加载时在活动合成器上切换音源。
    bool load(const QString& soundFontPath, QString* error);
    // 创建无设备驱动的合成器；采样率限 44100/48000 Hz，可选节拍器。
    bool loadOffline(const QString& soundFontPath, int sampleRate, bool metronome, QString* error);
    // 生成 frames 帧分离声道 PCM，先清空输出再累加歌曲和可选点击。
    bool renderOffline(int frames, float* left, float* right, QString* error);
    // 返回当前已探测的音源格式能力快照。
    FluidSynthCapabilities capabilities() const;
    // 在当前音源的 bank 0 中选择轨道初始音色，失败时提供诊断。
    bool configureTrack(int channel, int program, QString* error);
    // 查询能否开始提交事件；实时驱动已在 load 阶段创建。
    bool start();
    // 查询加载状态；活动音符清理由会话另行调用 flush 完成。
    bool pause();
    // 停止通过 flush 清理音符和通道状态，不销毁驱动。
    bool stop();
    // 跳转只清理合成器，时间定位和状态重放由播放会话负责。
    bool seek(qint64 microseconds);
    // 即时后端无需设置内部传输位置，返回加载状态。
    bool setTransportPosition(qint64 microseconds);
    // 当前后端不发布设备时钟，返回 -1 供传输层回退软件时钟。
    qint64 clockPositionUs() const;
    // 当前仅即时执行 MIDI，不支持按时间戳预排队。
    bool supportsTimedEvents() const;
    // 只有独立点击合成器和音源均准备好时才返回 true。
    bool supportsMetronome() const;
    // 在非实时路径提取并加载内置点击音源，配置独立通道。
    bool prepareMetronome(QString* error);
    // 立即触发强拍或弱拍采样；短采样自行结束。
    void submitMetronomeClick(bool accent);
    // 关闭独立点击合成器的所有声部，不影响歌曲通道。
    void stopMetronome();
    // 停止点击并重置歌曲合成器，供暂停、停止和状态重建使用。
    bool flush();
    // 按事件种类即时调用 MIDI API；调度时刻由播放层决定。
    void submit(const playback::PlaybackEvent& event);
    // 触发音符；通道映射到 16 个标准 MIDI 通道。
    void noteOn(int channel, int pitch, int velocity);
    // 释放指定通道/音高，由合成器按踏板和包络继续处理尾音。
    void noteOff(int channel, int pitch);
    // 合并两个 bank 字节，并在当前 SoundFont 中选择 program。
    void programChange(int channel, int program, int bankMsb = 0, int bankLsb = 0);
    // 发送 7 位控制器编号和值；缺少可选 API 时不执行。
    void controlChange(int channel, int controller, int value);
    // 发送限定到 0..16383 的通道弯音值。
    void pitchBend(int channel, int value);
    // 发送限定到 7 位范围的通道压力。
    void channelPressure(int channel, int value);
    // 发送指定音高的键压力；当前音高和值均限定到 7 位范围。
    void polyPressure(int channel, int pitch, int value);

private:
    // 离线集成测试可访问真实合成器，无需打开音频设备。
    friend struct FluidSynthEngineTestAccess;
    // 按平台查找动态库并解析必需/可选符号。
    bool resolveSymbols(QString* error);
    // 重建设置与合成器；可选择按需采样加载、采样率和节拍器声部池。
    bool initializeSynth(const QString& soundFontPath, QString* error,
                         bool dynamicSampleLoading = true, int sampleRate = 0,
                         bool metronome = true);
    // 按歌曲采样率创建无效果、低复音数的独立点击合成器。
    void initializeMetronomeSynth();
    // 合成器释放后再销毁设置和临时音源文件。
    void releaseMetronomeSynth();
    // 原生实时回调：直接在驱动输出缓冲区累加歌曲和点击，无分配或加锁。
    static int renderAudio(void* context, int frames, int effectCount,
                           float** effects, int outputCount, float** outputs) noexcept;
    // 先加载候选音源再替换 ID，失败时保留当前音源和驱动。
    bool loadSoundFontIntoActiveSynth(const QString& soundFontPath, QString* error);
    // 检查容器、串行化库日志捕获并执行 sfload。
    bool loadIntoSynth(fluid_synth_t* synth, const QString& soundFontPath, int resetPresets, int* soundFontId,
                       QString* error);
    // 仅根据确定证据更新 SF3 支持状态，普通加载失败仍保持未知。
    void updateFormatCapability(SoundFontFormat format, bool loaded, const QStringList& logs);
    // 停止驱动回调后再释放其可访问的合成器和设置。
    void release();
    // 从内置音源选择专用 bank/preset，并禁用点击通道的踏板和效果。
    bool configureMetronomeChannel();

    QLibrary m_library; // 函数指针的宿主，所有原生资源释放前必须保持库已加载。
    fluid_settings_t* m_settings = nullptr; // 歌曲采样率、增益及驱动配置，晚于合成器释放。
    fluid_synth_t* m_synth = nullptr; // 歌曲专用声部池，可在同一实例上事务式切换用户音源。
    fluid_audio_driver_t* m_driver = nullptr; // 实时驱动及回调所有者，离线模式不创建。
    // 点击声部池在驱动启动前创建，回调停止后才能销毁；与歌曲混入同一输出缓冲区。
    fluid_settings_t* m_metronomeSettings = nullptr;
    fluid_synth_t* m_metronomeSynth = nullptr;
    int m_soundFontId = -1;
    int m_metronomeSoundFontId = -1;
    std::unique_ptr<QTemporaryFile> m_metronomeFile; // 保持内置 sfload 文件存活，晚于点击合成器释放。
    bool m_metronomeReady = false; // 点击音源和通道配置均已完成，不只表示合成器已创建。
    bool m_loaded = false; // 歌曲资源已就绪；离线模式同样置位，不等于音频设备驱动已创建。
    FluidSynthCapabilities m_capabilities;

    // 与动态库 C ABI 对应的函数指针类型，避免编译期链接 FluidSynth。
    // 设置与日志 API 用于初始化和非实时加载诊断。
    using NewSettings = fluid_settings_t* (*)();
    using DeleteSettings = void (*)(fluid_settings_t*);
    using SettingsSetNum = int (*)(fluid_settings_t*, const char*, double);
    using SettingsGetNum = int (*)(fluid_settings_t*, const char*, double*);
    using SettingsSetInt = int (*)(fluid_settings_t*, const char*, int);
    using SettingsSetStr = int (*)(fluid_settings_t*, const char*, const char*);
    using FluidLogFunction = void (*)(int, const char*, void*);
    using SetLogFunction = FluidLogFunction (*)(int, FluidLogFunction, void*);
    using VersionString = const char* (*)();
    // 合成器、驱动和处理 API 定义原生资源生命周期与 PCM 回调契约。
    using NewSynth = fluid_synth_t* (*)(fluid_settings_t*);
    using DeleteSynth = int (*)(fluid_synth_t*);
    using NewAudioDriver = fluid_audio_driver_t* (*)(fluid_settings_t*, fluid_synth_t*);
    using AudioCallback = int (*)(void*, int, int, float**, int, float**);
    using NewAudioDriver2 = fluid_audio_driver_t* (*)(fluid_settings_t*, AudioCallback, void*);
    using Process = int (*)(fluid_synth_t*, int, int, float**, int, float**);
    using DeleteAudioDriver = void (*)(fluid_audio_driver_t*);
    // 音源和 MIDI API 提供即时演奏；可选控制能力以实际符号是否存在为准。
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

    // 符号仅在动态库成功加载并验证后使用；调用方按能力检查可选指针。
    NewSettings m_newSettings = nullptr;
    DeleteSettings m_deleteSettings = nullptr;
    SettingsSetNum m_settingsSetNum = nullptr;
    SettingsGetNum m_settingsGetNum = nullptr;
    SettingsSetInt m_settingsSetInt = nullptr;
    SettingsSetStr m_settingsSetStr = nullptr;
    SetLogFunction m_setLogFunction = nullptr;
    VersionString m_versionString = nullptr;
    NewSynth m_newSynth = nullptr;
    DeleteSynth m_deleteSynth = nullptr;
    NewAudioDriver m_newAudioDriver = nullptr;
    NewAudioDriver2 m_newAudioDriver2 = nullptr;
    Process m_process = nullptr;
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
    static constexpr int kMetronomeChannel = 0;
};

} // namespace midi_play::audio
