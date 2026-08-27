#pragma once

#include "domain/playback/playbacktypes.h"

#include <QMap>
#include <QString>

namespace midi_play::resources {

struct TrackAudioSettings {
    QString trackId;
    playback::AudioResource resource;
    int volume = 100;
    int pan = 64;
    bool mute = false;
    bool solo = false;
};

class ProjectAudioSettings final {
public:
    void setActiveSoundProfile(const QString& name) { m_activeProfile = name; }
    QString activeSoundProfile() const { return m_activeProfile; }
    void setTrack(const TrackAudioSettings& settings) { m_tracks.insert(settings.trackId, settings); }
    TrackAudioSettings track(const QString& id) const { return m_tracks.value(id); }

    bool read(const QString& path, QString* error);
    bool write(const QString& path, QString* error) const;

private:
    QString m_activeProfile = QStringLiteral("Basic");
    QMap<QString, TrackAudioSettings> m_tracks;
};

} // namespace midi_play::resources
