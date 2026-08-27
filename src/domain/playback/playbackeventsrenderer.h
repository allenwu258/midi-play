#pragma once

#include "playbackcontext.h"
#include "playbacktypes.h"

#include <memory>

namespace midi_play::playback {

class PlaybackEventsRenderer final {
public:
    explicit PlaybackEventsRenderer(std::shared_ptr<const PlaybackContext> context);
    QVector<PlaybackEvent> render(const PlaybackData& source) const;

private:
    std::shared_ptr<const PlaybackContext> m_context;
};

} // namespace midi_play::playback
