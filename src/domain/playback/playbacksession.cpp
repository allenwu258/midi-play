#include "playbacksession.h"
// 实现播放会话的生命周期、实时调度、seek 恢复和音源切换。
#include <algorithm>
#include <QHash>

namespace midi_play::playback {

// 构建播放模型、事件调度器和节拍器时间线；所有重计算均在会话启动前完成。
PlaybackSession::PlaybackSession(std::shared_ptr<const music::MusicDocument> document,
                                 std::unique_ptr<IPlaybackAudioService> audioService,
                                 QObject* parent)
    : QObject(parent)
    , m_document(std::move(document))
    , m_audioService(std::move(audioService))
    , m_playbackModel(m_document)
    , m_audioCapabilities(m_audioService->capabilities())
    , m_durationUs(m_playbackModel.durationUs())
{
    // 2 ms 精确定时器只负责推进调度；UI 位置发布由控制器单独节流。
    m_timer = new QTimer(this);
    m_timer->setInterval(2);
    m_timer->setTimerType(Qt::PreciseTimer);
    connect(m_timer, &QTimer::timeout, this, &PlaybackSession::onTimer);
    m_scheduler.setTracks(&m_playbackModel.tracks());
    m_scheduler.setGeneration(m_eventGeneration);
    m_metronomeTimeline = std::make_unique<MetronomeTimeline>(m_document, m_playbackModel.timeline());
    m_metronomeUnavailableReason = !m_metronomeTimeline->available()
        ? m_metronomeTimeline->unavailableReason() : QStringLiteral("等待音源加载");
    m_playHead.configure(48000);
    m_audioService->setEventGeneration(m_eventGeneration);
}

PlaybackSession::~PlaybackSession() = default;

bool PlaybackSession::loadSoundFont(const QString& path, QString* error)
{
    // 切换音源前保存播放状态；播放中必须先停止事件和节拍器，避免新旧后端重叠。
    if (error) error->clear();
    const bool wasPlaying = m_state == State::Playing;
    qint64 resumePositionUs = m_positionUs;
    if (wasPlaying) {
        resumePositionUs = m_playHead.positionUs();
        m_positionUs = resumePositionUs;
        m_timer->stop();
        m_audioService->stopMetronome();
        // 消费当前位置之前的点击，恢复后不会重复补发旧节拍。
        m_metronomeScheduler.takeDue(*m_metronomeTimeline, resumePositionUs, m_playHead.rate());
        advanceEventGeneration();
        flushActiveNotes();
        m_audioService->pause();
        m_playHead.pause(resumePositionUs);
    }

    if (!m_audioService->loadSoundFont(path, error)) {
        if (wasPlaying) {
            resumeAfterSoundFontChange(resumePositionUs, nullptr);
        }
        return false;
    }
    prepareMetronome();
    // 音源加载成功后逐轨注册事件数据；任一轨失败都按原播放位置恢复旧事务。
    for (const auto& track : m_playbackModel.tracks()) {
        if (!m_audioService->addTrack(track, error)) {
            if (wasPlaying) {
                resumeAfterSoundFontChange(resumePositionUs, nullptr);
            }
            return false;
        }
    }
    if (wasPlaying && !resumeAfterSoundFontChange(resumePositionUs, error)) {
        return false;
    }
    return true;
}

void PlaybackSession::loadSoundFontAsync(const QString& path)
{
    QString error;
    const bool success = loadSoundFont(path, &error);
    emit soundFontLoadFinished(success, error);
}

bool PlaybackSession::resumeAfterSoundFontChange(qint64 positionUs, QString* error)
{
    // 音源切换后的恢复与普通 play 相同，但必须保留调用前的播放位置。
    m_positionUs = std::clamp<qint64>(positionUs, 0, m_durationUs);
    if (!m_audioService->start()) {
        if (error) *error = QStringLiteral("无法在切换音源后恢复音频引擎");
        setState(State::Error);
        return false;
    }
    if (!m_audioService->setTransportPosition(m_positionUs)) {
        if (error) *error = QStringLiteral("无法在切换音源后恢复播放位置");
        setState(State::Error);
        return false;
    }
    rebuildAudioState(m_positionUs);
    m_playHead.start(m_positionUs);
    m_timer->start();
    emitPosition();
    return true;
}

void PlaybackSession::play()
{
    // 已在播放或文档非法时不重复启动后端。
    if (m_state == State::Playing || !m_document || !m_document->isValid()) {
        return;
    }
    startPlaybackFromCurrentPosition();
}

bool PlaybackSession::startPlaybackFromCurrentPosition()
{
    // 启动顺序固定为后端 start -> 设置传输位置 -> 重建 MIDI 状态 -> 启动播放头。
    if (!m_document || !m_document->isValid()) {
        return false;
    }
    if (!m_audioService->start()) {
        emit errorOccurred(QStringLiteral("无法启动音频引擎"));
        setState(State::Error);
        return false;
    }
    m_audioService->setTransportPosition(m_positionUs);
    rebuildAudioState(m_positionUs);
    setState(State::Playing);
    m_clockBaseUs = m_positionUs;
    m_playHead.start(m_positionUs);
    m_timer->start();
    return true;
}

void PlaybackSession::pause()
{
    if (m_state != State::Playing) {
        return;
    }
    if (m_audioCapabilities.usesAudioClock()) {
        // 硬件时钟优先于软件估算，暂停前先采样最后一个音频位置。
        updateAudioClockPosition();
    }
    m_positionUs = m_playHead.positionUs();
    m_timer->stop();
    m_audioService->stopMetronome();
    m_metronomeScheduler.takeDue(*m_metronomeTimeline, m_positionUs, m_playHead.rate());
    // 会话负责显式 NoteOff；暂停时冲刷活动音符，恢复时再重建精确状态。
    flushActiveNotes();
    m_audioService->pause();
    m_playHead.pause(m_positionUs);
    // 暂停是传输边界，先发布最终位置，再发送状态变化，避免 UI 保留旧帧。
    emitPosition();
    setState(State::Paused);
}

void PlaybackSession::stop()
{
    if (m_state == State::Empty) {
        return;
    }
    // stop 同时取消节拍器、提升事件代次并冲刷后端，确保旧队列不能回流。
    m_timer->stop();
    m_audioService->stopMetronome();
    advanceEventGeneration();
    flushActiveNotes();
    m_audioService->stop();
    m_positionUs = 0;
    m_clockBaseUs = 0;
    m_playHead.reset();
    m_scheduler.reset();
    m_metronomeScheduler.seek(*m_metronomeTimeline, 0);
    emitPosition();
    setState(State::Stopped);
}

void PlaybackSession::seek(qint64 microseconds)
{
    if (!m_document) {
        return;
    }
    // 播放中的 seek 暂停推进，完成后重新 start；暂停中的 seek 只更新位置。
    const bool resume = m_state == State::Playing;
    if (resume) {
        m_timer->stop();
    }
    m_audioService->stopMetronome();
    advanceEventGeneration();
    flushActiveNotes();
    m_positionUs = std::clamp<qint64>(microseconds, 0, m_durationUs);
    // 后端、播放头、事件游标和节拍器游标必须在同一事务中定位。
    m_audioService->seek(m_positionUs);
    m_clockBaseUs = m_positionUs;
    m_playHead.seek(m_positionUs);
    m_audioService->setTransportPosition(m_positionUs);
    m_scheduler.seek(m_positionUs);
    m_metronomeScheduler.seek(*m_metronomeTimeline, m_positionUs);
    emitPosition();
    if (resume) {
        startPlaybackFromCurrentPosition();
    }
}

bool PlaybackSession::setPlaybackRatePercent(int percent)
{
    const int normalized = midi_play::settings::normalizePlaybackRatePercent(percent);
    if (m_playbackRatePercent == normalized) return true;
    if (!supportsPlaybackRate()) return false;

    // 只调整传输时钟倍率；调度游标、音符状态和代次保持不变，
    // MIDI 与可视化继续消费同一音乐时间。
    m_playbackRatePercent = normalized;
    m_playHead.setRate(static_cast<double>(normalized) / 100.0);
    if (m_state == State::Playing) {
        m_positionUs = m_playHead.positionUs();
        emitPosition();
    }
    return true;
}

void PlaybackSession::setMetronomeEnabled(bool enabled)
{
    if (m_metronomeEnabled == enabled) return;

    m_metronomeEnabled = enabled;
    if (!enabled) {
        m_audioService->stopMetronome();
        return;
    }

    // 开启时从当前传输位置重新定位，关闭时只停止点击，不影响 MIDI。
    const qint64 position = m_state == State::Playing
        ? m_playHead.positionUs() : m_positionUs;
    m_metronomeScheduler.seek(*m_metronomeTimeline, position);
}

void PlaybackSession::onTimer()
{
    if (m_state != State::Playing || !m_document) {
        return;
    }
    // 每个 tick 先解析权威位置，再收集事件；带时间戳后端预排约 30 ms。
    if (m_audioCapabilities.usesAudioClock()) {
        updateAudioClockPosition();
    }
    m_positionUs = m_playHead.positionUs();
    const qint64 now = m_positionUs;
    const qint64 dispatchUntilUs = m_audioCapabilities.timedEvents ? now + 30'000 : now;
    const auto window = m_scheduler.collectWindow(now, dispatchUntilUs, m_eventGeneration);
    if (!window.events.isEmpty()) {
        m_audioService->submitBatch(window.events, window.generation);
    }
    if (m_positionUs >= m_durationUs) {
        // 到达时长后通过统一 stop 流程清理后端和游标。
        stop();
        return;
    }
    dispatchMetronome(now);
    emitPosition();
}

void PlaybackSession::dispatchMetronome(qint64 positionUs)
{
    // 节拍器点击与 MIDI 事件共享代次；关闭或不可用时不进入后端。
    if (!m_metronomeEnabled || !m_metronomeAvailable) return;
    if (const auto beat = m_metronomeScheduler.takeDue(*m_metronomeTimeline,
                                                      positionUs, m_playHead.rate())) {
        m_audioService->submitMetronomeClick(
            beat->accent == MetronomeAccent::Measure, m_eventGeneration);
    }
}

void PlaybackSession::prepareMetronome()
{
    // 节拍器是可选能力：网格、后端声明和资源准备任一失败都只影响节拍器。
    QString reason;
    if (!m_metronomeTimeline->available()) reason = m_metronomeTimeline->unavailableReason();
    else if (!m_audioCapabilities.supportsMetronome()) reason = QStringLiteral("当前音频后端不支持节拍器");
    else if (!m_audioService->prepareMetronome(&reason) && reason.isEmpty())
        reason = QStringLiteral("无法准备节拍器音源");
    m_metronomeAvailable = reason.isEmpty();
    m_metronomeUnavailableReason = reason;
    emit metronomeAvailabilityChanged(m_metronomeAvailable, reason);
}

void PlaybackSession::rebuildAudioState(qint64 targetUs)
{
    // seek/恢复时从最近快照开始重放到 targetUs，重建通道控制器和活动音符。
    m_scheduler.seek(targetUs);

    const auto& globalEvents = m_playbackModel.globalEvents();
    const int end = m_playbackModel.globalIndex()
        ? m_playbackModel.globalIndex()->lowerBound(targetUs)
        : 0;
    int start = 0;
    QHash<qint64, QVector<PlaybackEvent>> activeNotes;
    QVector<PlaybackEvent> replayEvents;

    const PlaybackStateSnapshot* snapshot = nullptr;
    for (const auto& candidate : m_playbackModel.globalSnapshots()) {
        if (candidate.timestampUs > targetUs) break;
        snapshot = &candidate;
    }
    if (snapshot) {
        // 快照先恢复已知通道状态和仍在发声的音符，再重放快照之后的事件。
        start = std::min(snapshot->eventIndex, end);
        for (auto channelIt = snapshot->channels.cbegin(); channelIt != snapshot->channels.cend(); ++channelIt) {
            const int channel = channelIt.key();
            const auto& state = channelIt.value();
            if (!state.initialized) continue;
            // Bank Select 由两个控制器组成，恢复通道时必须先于 Program Change。
            for (const int controller : {0, 32}) {
                if (!state.controllers.contains(controller)) continue;
                PlaybackEvent control;
                control.channel = channel;
                control.controller = controller;
                control.value = state.controllers.value(controller);
                control.kind = PlaybackEventKind::ControlChange;
                replayEvents.push_back(control);
            }
            PlaybackEvent program;
            program.channel = channel;
            program.program = state.program;
            program.bankMsb = state.bankMsb;
            program.bankLsb = state.bankLsb;
            program.kind = PlaybackEventKind::ProgramChange;
            replayEvents.push_back(program);
            QVector<int> controllerKeys;
            controllerKeys.reserve(state.controllers.size());
            for (auto controllerIt = state.controllers.cbegin(); controllerIt != state.controllers.cend(); ++controllerIt) {
                if (controllerIt.key() != 0 && controllerIt.key() != 32) controllerKeys.push_back(controllerIt.key());
            }
            const auto controllerPriority = [](int controller) {
                switch (controller) {
                case 101: return 0; // RPN MSB（最高有效字节）
                case 100: return 1; // RPN LSB（最低有效字节）
                case 99: return 2;  // NRPN MSB（最高有效字节）
                case 98: return 3;  // NRPN LSB（最低有效字节）
                case 6: return 4;   // Data Entry MSB（最高有效字节）
                case 38: return 5;  // Data Entry LSB（最低有效字节）
                default: return 100 + controller;
                }
            };
            std::sort(controllerKeys.begin(), controllerKeys.end(), [&](int left, int right) {
                const int leftPriority = controllerPriority(left);
                const int rightPriority = controllerPriority(right);
                return leftPriority != rightPriority ? leftPriority < rightPriority : left < right;
            });
            for (const int controller : controllerKeys) {
                PlaybackEvent control;
                control.channel = channel;
                control.controller = controller;
                control.value = state.controllers.value(controller);
                control.kind = PlaybackEventKind::ControlChange;
                replayEvents.push_back(control);
            }
            PlaybackEvent bend;
            bend.channel = channel;
            bend.value = state.pitchBend;
            bend.kind = PlaybackEventKind::PitchBend;
            replayEvents.push_back(bend);
            PlaybackEvent pressure;
            pressure.channel = channel;
            pressure.value = state.channelPressure;
            pressure.kind = PlaybackEventKind::ChannelPressure;
            replayEvents.push_back(pressure);
            for (auto pressureIt = state.polyPressure.cbegin(); pressureIt != state.polyPressure.cend(); ++pressureIt) {
                PlaybackEvent polyPressure;
                polyPressure.channel = channel;
                polyPressure.pitch = pressureIt.key();
                polyPressure.value = pressureIt.value();
                polyPressure.kind = PlaybackEventKind::PolyPressure;
                replayEvents.push_back(polyPressure);
            }
        }
        for (const auto& active : snapshot->activeNotes) {
            // 将快照中的活动音符暂时重建为 targetUs 之前开始的 NoteOn。
            PlaybackEvent resumed;
            resumed.timestampUs = targetUs - 1;
            resumed.durationUs = active.endTimestampUs - targetUs + 1;
            resumed.channel = active.channel;
            resumed.pitch = active.pitch;
            resumed.noteId = active.noteId;
            resumed.velocity = active.velocity;
            resumed.keyReleased = active.keyReleased;
            resumed.kind = PlaybackEventKind::NoteOn;
            const qint64 key = active.noteId >= 0 ? active.noteId
                                                  : qint64(active.channel * 128 + active.pitch);
            activeNotes[key].push_back(resumed);
        }
    }

    for (int i = start; i < end; ++i) {
        const auto& event = globalEvents[i];
        if (event.kind == PlaybackEventKind::NoteOn) {
            const qint64 key = event.noteId >= 0 ? event.noteId
                                                : qint64(event.channel * 128 + event.pitch);
            activeNotes[key].push_back(event);
        } else if (event.kind == PlaybackEventKind::NoteOff) {
            const qint64 key = event.noteId >= 0 ? event.noteId
                                                 : qint64(event.channel * 128 + event.pitch);
            if (activeNotes.contains(key) && !activeNotes[key].isEmpty()) {
                activeNotes[key].removeLast();
                if (activeNotes[key].isEmpty()) activeNotes.remove(key);
            }
        } else {
            // 快照后的状态事件按全局规范顺序重放，避免不同轨道无序覆盖共享通道。
            replayEvents.push_back(event);
        }
    }

    for (auto it = activeNotes.cbegin(); it != activeNotes.cend(); ++it) {
        for (const auto& active : it.value()) {
            PlaybackEvent resumed = active;
            const qint64 endUs = resumed.timestampUs + resumed.durationUs;
            if (endUs <= targetUs) {
                continue;
            }
            resumed.timestampUs = targetUs;
            resumed.durationUs = endUs - targetUs;
            replayEvents.push_back(resumed);
            if (active.keyReleased) {
                // 已释放但被踏板保持的音符需要立即补发 NoteOff，维持释放语义。
                PlaybackEvent released = resumed;
                released.durationUs = 0;
                released.kind = PlaybackEventKind::NoteOff;
                replayEvents.push_back(released);
            }
        }
    }
    if (!replayEvents.isEmpty()) {
        m_audioService->submitBatch(replayEvents, m_eventGeneration);
    }
}

void PlaybackSession::flushActiveNotes()
{
    // 后端 flush 是暂停、停止、seek 和音源切换的共同清理边界。
    m_audioService->flush();
}

void PlaybackSession::advanceEventGeneration()
{
    // 代次递增后，后端和调度窗口都会拒绝此前生成的异步事件。
    ++m_eventGeneration;
    m_scheduler.setGeneration(m_eventGeneration);
    m_audioService->setEventGeneration(m_eventGeneration);
}

void PlaybackSession::updateAudioClockPosition()
{
    // 音频时钟不可用时清除旧值，避免继续使用过期硬件位置。
    const qint64 audioClockUs = m_audioService->clockPositionUs();
    if (audioClockUs >= 0) m_playHead.setAudioPositionUs(audioClockUs);
    else m_playHead.clearAudioPosition();
}

void PlaybackSession::setState(State state)
{
    if (m_state == state) {
        return;
    }
    m_state = state;
    emit stateChanged(m_state);
}

// 位置通知只携带会话当前精确值，控制器负责跨线程节流。
void PlaybackSession::emitPosition()
{
    emit positionChanged(m_positionUs, m_durationUs);
}

} // namespace midi_play::playback
