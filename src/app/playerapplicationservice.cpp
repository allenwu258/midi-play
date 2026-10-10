// 播放应用服务实现：在资源验证与完整模型构建成功后发布新的当前状态。
#include "playerapplicationservice.h"

#include "infrastructure/audio/fluidsynthengine.h"
#include "infrastructure/audio/fluidsynthaudioservice.h"
#include "infrastructure/audio/soundfontinspector.h"
#include "infrastructure/audio/threadedplaybackaudioservice.h"
#include "infrastructure/readers/musicxmlreaderadapter.h"
#include "infrastructure/readers/midireaderadapter.h"
#include "domain/visualization/playbackvisualizationprojector.h"

#include <QFileInfo>
#include <QtConcurrent>

namespace midi_play::app {
namespace {

// 后台导入结果同时携带音乐文档、视觉投影及各自错误，避免发布半成品。
struct LoadedProject {
    music::ReadResult readResult;
    visualization::VisualChartPtr visualChart;
    QString visualizationError;
    quint64 generation = 0;
};

} // namespace

// 注册输入格式与信号类型；无会话音源验证完成后复用统一结果发布流程。
PlayerApplicationService::PlayerApplicationService(QObject* parent)
    : QObject(parent)
{
    qRegisterMetaType<playback::State>();
    qRegisterMetaType<visualization::VisualChartPtr>();
    m_readerRegistry.registerReader(std::make_unique<readers::MusicXmlReaderAdapter>());
    m_readerRegistry.registerReader(std::make_unique<readers::MidiReaderAdapter>());
    connect(&m_soundFontValidationWatcher, &QFutureWatcher<QString>::finished, this, [this] {
        const QString validationError = m_soundFontValidationWatcher.result();
        if (!validationError.isEmpty()) {
            completeSoundFontLoad(false, validationError);
            return;
        }
        completeSoundFontLoad(true, {});
    });
}

void PlayerApplicationService::openMusicXml(const QString& path)
{
    openFile(path);
}

// 异步构建文档及视觉投影；仅最新请求可以在宿主线程替换当前控制器。
void PlayerApplicationService::openFile(const QString& path)
{
    if (m_soundFontLoading) {
        emit errorOccurred(QStringLiteral("音源加载仍在进行中，请稍后再打开乐曲"));
        return;
    }
    const auto suffix = QFileInfo(path).suffix();
    const auto* reader = m_readerRegistry.find(suffix);
    if (!reader) {
        emit errorOccurred(QStringLiteral("不支持的音乐文件类型: %1").arg(suffix));
        return;
    }
    // 导入失败保留原会话；代次只决定结果发布资格，不中断先前后台读取。
    const quint64 generation = ++m_loadGeneration;
    m_documentLoading = true;
    emit busyChanged(true);
    auto watcher = new QFutureWatcher<LoadedProject>(this);
    connect(watcher, &QFutureWatcher<LoadedProject>::finished, this, [this, watcher, path, generation] {
        const auto result = watcher->result();
        watcher->deleteLater();
        // 新请求使旧结果失去发布资格；旧任务仍可自行完成和释放 watcher。
        if (generation != m_loadGeneration) return;
        m_documentLoading = false;
        emit busyChanged(false);
        if (!result.readResult.ok()) {
            emit errorOccurred(result.readResult.error);
            return;
        }
        if (!result.visualChart) {
            emit errorOccurred(result.visualizationError.isEmpty()
                ? QStringLiteral("无法建立播放可视化") : result.visualizationError);
            return;
        }
        // 先构建独立控制器及线程音频适配器，成功初始化后再替换当前会话。
        auto engine = std::make_unique<audio::FluidSynthEngine>();
        auto fluidsynthService = std::make_unique<audio::FluidSynthAudioService>(std::move(engine));
        auto audioService = std::make_unique<audio::ThreadedPlaybackAudioService>(std::move(fluidsynthService));
        auto newController = std::make_unique<playback::PlaybackController>(this);
        newController->setPositionPublishRate(m_visualizationRefreshRate);
        newController->setPlaybackRatePercent(m_playbackRatePercent);
        newController->setMetronomeEnabled(m_metronomeEnabled);
        QString controllerError;
        if (!newController->setDocument(result.readResult.document, std::move(audioService),
                                        &controllerError)) {
            emit errorOccurred(controllerError);
            return;
        }
        QString soundFontError;
        if (!m_soundFontPath.isEmpty()
            && !newController->loadSoundFont(m_soundFontPath, &soundFontError)) {
            // 外部音源失效时仍保留已解析乐曲，用户可为同一会话重新选择音源。
            m_soundFontPath.clear();
            if (soundFontError.isEmpty()) soundFontError = QStringLiteral("无法为乐曲加载音源");
        }

        m_controller = std::move(newController);
        m_document = result.readResult.document;
        emit metronomeAvailabilityChanged(m_controller->supportsMetronome(),
                                          m_controller->metronomeUnavailableReason());
        m_fileName = path;
        connectSession();
        m_positionUs = 0;
        m_durationUs = session()->durationMicroseconds();
        m_playbackState = playback::State::Ready;
        // 先发布图表，再发布文档与传输状态，使 GUI 的后续位置更新已有可视数据。
        emit visualizationReady(result.visualChart);
        emit documentLoaded(result.readResult.document->title().isEmpty()
                                ? QFileInfo(path).fileName() : result.readResult.document->title(),
                            m_durationUs);
        emit positionChanged(m_positionUs, m_durationUs);
        emit playbackStateChanged(m_playbackState);
        if (!soundFontError.isEmpty()) reportSoundFontFailure(soundFontError);
    });
    // 后台顺序完成读入与视觉投影；GUI 回调统一验证结果后再发布新会话。
    watcher->setFuture(QtConcurrent::run([reader, path, generation] {
        LoadedProject result;
        result.generation = generation;
        result.readResult = reader->read(path);
        if (result.readResult.ok()) {
            visualization::VisualizationProjectionOptions options;
            options.fallbackTitle = QFileInfo(path).completeBaseName();
            result.visualChart = visualization::PlaybackVisualizationProjector().project(
                *result.readResult.document, generation, options, &result.visualizationError);
        }
        return result;
    }));
}

// 串行受理音源请求：有控制器则异步加载，无控制器则后台验证并记住选择。
void PlayerApplicationService::requestSoundFontLoad(const QString& path)
{
    if (m_soundFontLoading || m_documentLoading) {
        reportSoundFontFailure(QStringLiteral("正在加载资源，请稍后再选择音源"));
        return;
    }

    QString normalizedPath;
    if (!normalizeSoundFontPath(path, &normalizedPath)) {
        emit soundFontLoadFinished(false);
        return;
    }

    m_pendingSoundFontPath = normalizedPath;
    m_lastSoundFontError.clear();
    setSoundFontLoading(true);
    if (m_controller) {
        m_controller->loadSoundFontAsync(normalizedPath);
        return;
    }

    // 无乐曲会话时只验证音源可加载性，成功后保存选择供后续会话使用。
    m_soundFontValidationWatcher.setFuture(QtConcurrent::run([normalizedPath] {
        audio::FluidSynthEngine validator;
        QString error;
        if (validator.validateSoundFont(normalizedPath, &error)) return QString();
        return error.isEmpty() ? QStringLiteral("音源验证失败") : error;
    }));
}

// 同步加载入口；只有验证/加载成功才发布选择提交信号，失败尝试恢复旧音源。
bool PlayerApplicationService::loadSoundFont(const QString& path)
{
    if (m_soundFontLoading || m_documentLoading) {
        reportSoundFontFailure(QStringLiteral("正在加载资源，请稍后再选择音源"));
        return false;
    }
    QString normalizedPath;
    if (!validateSoundFontFile(path, &normalizedPath)) {
        return false;
    }

    if (!session()) {
        audio::FluidSynthEngine validator;
        QString validationError;
        if (!validator.validateSoundFont(normalizedPath, &validationError)) {
            reportSoundFontFailure(validationError);
            return false;
        }
        m_soundFontPath = normalizedPath;
        m_lastSoundFontError.clear();
        emit soundFontLoaded(normalizedPath);
        emit soundFontSelectionCommitted(normalizedPath);
        return true;
    }

    const QString previousPath = m_soundFontPath;
    QString error;
    if (!m_controller->loadSoundFont(normalizedPath, &error)) {
        // 新音源加载可能改变合成器状态，失败后尝试恢复先前有效选择。
        QString recoveryError;
        if (!previousPath.isEmpty() && previousPath != normalizedPath
            && !m_controller->loadSoundFont(previousPath, &recoveryError)) {
            error += QStringLiteral("；恢复原音源失败: %1").arg(recoveryError);
        }
        reportSoundFontFailure(error);
        return false;
    }
    m_soundFontPath = normalizedPath;
    m_lastSoundFontError.clear();
    emit soundFontLoaded(normalizedPath);
    emit soundFontSelectionCommitted(normalizedPath);
    return true;
}

bool PlayerApplicationService::validateSoundFontFile(const QString& path,
                                                     QString* normalizedPath)
{
    QString normalized;
    if (!normalizeSoundFontPath(path, &normalized)) {
        return false;
    }

    QString inspectionError;
    const auto inspection = audio::SoundFontInspector::inspect(normalized,
                                                                &inspectionError);
    if (!inspection.validRiffContainer) {
        reportSoundFontFailure(inspectionError.isEmpty()
            ? QStringLiteral("音源文件容器无效: %1").arg(path) : inspectionError);
        return false;
    }

    if (normalizedPath) {
        *normalizedPath = normalized;
    }
    return true;
}

// 检查文件可读性与扩展名，返回绝对路径；此步骤尚未检查 SoundFont 容器内容。
bool PlayerApplicationService::normalizeSoundFontPath(const QString& path,
                                                      QString* normalizedPath)
{
    if (path.trimmed().isEmpty()) {
        reportSoundFontFailure(QStringLiteral("尚未配置有效音源，请在设置中选择 SF2/SF3 音源后播放。"));
        return false;
    }
    const QFileInfo soundFontInfo(path.trimmed());
    if (!soundFontInfo.exists() || !soundFontInfo.isFile()
        || !soundFontInfo.isReadable()) {
        reportSoundFontFailure(QStringLiteral("无法读取音源文件: %1").arg(path));
        return false;
    }

    const QString suffix = soundFontInfo.suffix().toLower();
    if (suffix != QStringLiteral("sf2") && suffix != QStringLiteral("sf3")) {
        reportSoundFontFailure(QStringLiteral("不支持的音源文件类型: %1").arg(suffix));
        return false;
    }

    if (normalizedPath) {
        *normalizedPath = soundFontInfo.absoluteFilePath();
    }
    return true;
}

// 统一缓存最近音源错误并发布专用失败信号，供设置窗口和播放入口消费。
void PlayerApplicationService::reportSoundFontFailure(const QString& message)
{
    m_lastSoundFontError = message.isEmpty() ? QStringLiteral("音源加载失败") : message;
    emit soundFontLoadFailed(m_lastSoundFontError);
}

void PlayerApplicationService::setVisualizationRefreshRate(int refreshRate)
{
    const int normalizedRefreshRate = settings::normalizeVisualizationRefreshRate(refreshRate);
    if (m_visualizationRefreshRate == normalizedRefreshRate) {
        return;
    }

    m_visualizationRefreshRate = normalizedRefreshRate;
    if (m_controller) {
        m_controller->setPositionPublishRate(m_visualizationRefreshRate);
    }
}

// 先由控制器受理倍率请求，再更新应用快照；实际会话修改排队到播放线程执行。
void PlayerApplicationService::setPlaybackRatePercent(int percent)
{
    const int normalized = settings::normalizePlaybackRatePercent(percent);
    if (m_playbackRatePercent == normalized) return;

    // 控制器拒绝倍率请求时重新发布旧值，使控件回到已确认倍率。
    if (m_controller && !m_controller->setPlaybackRatePercent(normalized)) {
        emit playbackRateChanged(m_playbackRatePercent);
        return;
    }
    m_playbackRatePercent = normalized;
    emit playbackRateChanged(normalized);
}

void PlayerApplicationService::setMetronomeEnabled(bool enabled)
{
    if (m_metronomeEnabled == enabled) return;
    if (m_controller) m_controller->setMetronomeEnabled(enabled);
    m_metronomeEnabled = enabled;
    emit metronomeChanged(enabled);
}

void PlayerApplicationService::setGraphicsMode(settings::GraphicsMode mode)
{
    m_graphicsMode = settings::normalizeGraphicsMode(mode);
    emit graphicsModeChanged(m_graphicsMode);
}

// 播放前校验外部音源仍有效，再委托控制器执行状态转换。
void PlayerApplicationService::play()
{
    if (m_soundFontLoading) {
        reportSoundFontFailure(QStringLiteral("正在检查或加载音源，请稍后再播放。"));
        return;
    }
    // 每次播放重新检查外部音源的可读性/容器，及时报告移动或损坏的文件。
    if (!validateSoundFontFile(m_soundFontPath, nullptr)) return;
    if (m_controller) m_controller->play();
}
void PlayerApplicationService::pause() { if (m_controller) m_controller->pause(); }
void PlayerApplicationService::stop() { if (m_controller) m_controller->stop(); }
// 转发微秒跳转请求后发布实际位置的不连续通知，供视觉效果重置。
void PlayerApplicationService::seek(qint64 microseconds)
{
    if (!m_controller) return;
    m_controller->seek(microseconds);
    emit playbackDiscontinuity(m_positionUs);
}

// 在控制器替换后绑定新会话；旧控制器销毁会自动断开原有 QObject 连接。
void PlayerApplicationService::connectSession()
{
    connect(m_controller.get(), &playback::PlaybackController::metronomeAvailabilityChanged,
            this, &PlayerApplicationService::metronomeAvailabilityChanged);
    connect(m_controller.get(), &playback::PlaybackController::stateChanged, this, [this](playback::State state) {
        m_playbackState = state;
        emit playbackStateChanged(state);
    });
    connect(m_controller.get(), &playback::PlaybackController::errorOccurred, this, &PlayerApplicationService::errorOccurred);
    connect(m_controller.get(), &playback::PlaybackController::soundFontLoadFinished,
            this, &PlayerApplicationService::completeSoundFontLoad);
    connect(m_controller.get(), &playback::PlaybackController::positionChanged, this,
            [this](qint64 position, qint64 duration, qint64 sampledAtUs) {
                m_positionUs = position;
                m_durationUs = duration;
                emit positionChanged(position, duration, sampledAtUs);
            });
}

// 消费唯一待处理路径，并按加载状态、结果、选择提交的顺序通知界面。
void PlayerApplicationService::completeSoundFontLoad(bool success, const QString& error)
{
    // 消费候选并先结束加载状态；随后发具体失败或成功信号，供界面与启动流程接续。
    const QString path = m_pendingSoundFontPath;
    m_pendingSoundFontPath.clear();
    if (success) {
        m_soundFontPath = path;
        m_lastSoundFontError.clear();
    }
    setSoundFontLoading(false);
    if (!success) {
        reportSoundFontFailure(error);
        emit soundFontLoadFinished(false);
        return;
    }

    emit soundFontLoaded(path);
    emit soundFontSelectionCommitted(path);
    emit soundFontLoadFinished(true);
}

void PlayerApplicationService::setSoundFontLoading(bool loading)
{
    if (m_soundFontLoading == loading) {
        return;
    }
    m_soundFontLoading = loading;
    emit soundFontLoadingChanged(loading);
}

} // namespace midi_play::app
