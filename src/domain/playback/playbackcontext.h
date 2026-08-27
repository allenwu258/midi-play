#pragma once

#include "domain/music/musicdocument.h"

#include <QMap>
#include <memory>

namespace midi_play::playback {

class PlaybackContext final {
public:
    explicit PlaybackContext(std::shared_ptr<const music::MusicDocument> document);
    int velocityAt(const QString& trackId, qint64 timestampUs, int fallback) const;
    double tempoAt(qint64 timestampUs) const;

private:
    struct DynamicPoint { qint64 timestampUs; int velocity; };
    QMap<QString, QVector<DynamicPoint>> m_dynamics;
    QMap<qint64, double> m_tempos;
};

} // namespace midi_play::playback
