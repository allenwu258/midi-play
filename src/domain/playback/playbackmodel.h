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
    qint64 durationUs() const;
    const PlaybackData* track(const QString& id) const;
    const PlaybackContext& context() const { return *m_context; }

private:
    std::shared_ptr<const music::MusicDocument> m_document;
    QVector<PlaybackData> m_tracks;
    std::shared_ptr<PlaybackContext> m_context;
};

} // namespace midi_play::playback
