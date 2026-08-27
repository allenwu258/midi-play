#pragma once

#include "domain/music/musicdocument.h"
#include "playbacktypes.h"
#include "playbackcontext.h"
#include "playbackeventsrenderer.h"

#include <QVector>
#include <memory>

namespace midi_play::playback {

class PlaybackModel final {
public:
    explicit PlaybackModel(std::shared_ptr<const music::MusicDocument> document);
    const QVector<PlaybackData>& tracks() const { return m_tracks; }
    const QVector<PlaybackEvent>& globalEvents() const { return m_globalEvents; }
    const QVector<PlaybackStateSnapshot>& globalSnapshots() const { return m_globalSnapshots; }
    const std::shared_ptr<const PlaybackEventIndex>& globalIndex() const { return m_globalIndex; }
    qint64 durationUs() const;
    const PlaybackData* track(const QString& id) const;
    const PlaybackContext& context() const { return *m_context; }

private:
    std::shared_ptr<const music::MusicDocument> m_document;
    QVector<PlaybackData> m_tracks;
    QVector<PlaybackEvent> m_globalEvents;
    QVector<PlaybackStateSnapshot> m_globalSnapshots;
    std::shared_ptr<const PlaybackEventIndex> m_globalIndex;
    std::shared_ptr<PlaybackContext> m_context;
};

} // namespace midi_play::playback
