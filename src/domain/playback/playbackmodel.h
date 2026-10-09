#pragma once

// 音乐文档到播放事件、快照和谱面 DOM 的编译模型接口。
#include "domain/music/musicdocument.h"
#include "domain/music/playbacktimeline.h"
#include "domain/music/scoreplaybackdom.h"
#include "playbacktypes.h"
#include "playbackcontext.h"
#include "playbackeventsrenderer.h"

#include <QVector>
#include <memory>

namespace midi_play::playback {

// 将音乐域文档编译为播放时间线、后端事件流、状态快照和谱面 DOM。
class PlaybackModel final {
public:
    // 从已定稿的音乐文档一次性构建播放模型。
    explicit PlaybackModel(std::shared_ptr<const music::MusicDocument> document);
    // 返回按 staff/voice 拆分的轨道数据。
    const QVector<PlaybackData>& tracks() const { return m_tracks; }
    // 返回用于全局 seek 状态重建的规范事件流。
    const QVector<PlaybackEvent>& globalEvents() const { return m_globalEvents; }
    // 返回按固定时间间隔生成的全局状态快照。
    const QVector<PlaybackStateSnapshot>& globalSnapshots() const { return m_globalSnapshots; }
    const std::shared_ptr<const PlaybackEventIndex>& globalIndex() const { return m_globalIndex; }
    // 返回供播放和谱面渲染共享的层级 DOM。
    const std::shared_ptr<const music::ScorePlaybackDom>& scoreDom() const { return m_scoreDom; }
    qint64 durationUs() const { return m_timeline ? m_timeline->durationUs() : 0; }
    // 返回不可变的反复展开时间线。
    const std::shared_ptr<const music::PlaybackTimeline>& timeline() const { return m_timeline; }
    const PlaybackData* track(const QString& id) const;
    // 返回力度上下文；只读消费者无需重新扫描文档。
    const PlaybackContext& context() const { return *m_context; }

private:
    std::shared_ptr<const music::MusicDocument> m_document;
    std::shared_ptr<const music::PlaybackTimeline> m_timeline;
    std::shared_ptr<const music::ScorePlaybackDom> m_scoreDom;
    QVector<PlaybackData> m_tracks;
    QVector<PlaybackEvent> m_globalEvents;
    QVector<PlaybackStateSnapshot> m_globalSnapshots;
    std::shared_ptr<const PlaybackEventIndex> m_globalIndex;
    std::shared_ptr<PlaybackContext> m_context;
};

} // namespace midi_play::playback
