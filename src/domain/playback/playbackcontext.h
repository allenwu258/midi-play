#pragma once

#include "domain/music/playbacktimeline.h"

#include <QMap>
#include <memory>

namespace midi_play::playback {

class PlaybackContext final {
public:
    PlaybackContext(std::shared_ptr<const music::MusicDocument> document,
                    std::shared_ptr<const music::PlaybackTimeline> timeline);
    int velocityAt(const QString& trackId, qint64 timestampUs, int fallback) const;
    double tempoAt(qint64 timestampUs) const;

private:
    struct DynamicPoint { qint64 timestampUs; int velocity; };
    struct HairpinPoint { qint64 startUs; qint64 endUs; bool crescendo; };
    QMap<QString, QVector<DynamicPoint>> m_dynamics;
    QMap<QString, QVector<HairpinPoint>> m_hairpins;
    QMap<qint64, double> m_tempos;
};

} // namespace midi_play::playback
