#pragma once

#include "domain/music/musicdocument.h"

#include <QString>
#include <QVector>
#include <memory>
#include <algorithm>

namespace midi_play::playback {

enum class State { Empty, Ready, Playing, Paused, Stopped, Error };

struct AudioResource {
    QString path;
    int soundFontId = -1;
};

struct PlaybackSetupData {
    QString soundId = QStringLiteral("piano");
    QString musicXmlSoundId;
    bool supportsSingleNoteDynamics = false;
};

struct PlaybackEvent {
    qint64 timestampUs = 0;
    qint64 durationUs = 0;
    int channel = 0;
    int pitch = 60;
    int velocity = 90;
    int program = 0;
};

class PlaybackEventIndex final {
public:
    explicit PlaybackEventIndex(const QVector<PlaybackEvent>& events)
    {
        m_timestamps.reserve(events.size());
        for (const auto& event : events) m_timestamps.push_back(event.timestampUs);
    }

    int lowerBound(qint64 timestampUs) const
    {
        return static_cast<int>(std::lower_bound(m_timestamps.cbegin(), m_timestamps.cend(), timestampUs) - m_timestamps.cbegin());
    }

private:
    QVector<qint64> m_timestamps;
};

struct PlaybackData {
    QString trackId;
    PlaybackSetupData setupData;
    QVector<PlaybackEvent> events;
    std::shared_ptr<const PlaybackEventIndex> index;
};

} // namespace midi_play::playback
