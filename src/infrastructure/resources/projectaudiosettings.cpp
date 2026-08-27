#include "projectaudiosettings.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace midi_play::resources {

bool ProjectAudioSettings::read(const QString& path, QString* error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) *error = file.errorString();
        return false;
    }
    const auto root = QJsonDocument::fromJson(file.readAll()).object();
    m_activeProfile = root.value(QStringLiteral("activeSoundProfile")).toString(QStringLiteral("Basic"));
    m_tracks.clear();
    for (const auto& value : root.value(QStringLiteral("tracks")).toArray()) {
        const auto object = value.toObject();
        TrackAudioSettings settings;
        settings.trackId = object.value(QStringLiteral("trackId")).toString();
        settings.resource.path = object.value(QStringLiteral("resourcePath")).toString();
        settings.volume = object.value(QStringLiteral("volume")).toInt(100);
        settings.pan = object.value(QStringLiteral("pan")).toInt(64);
        settings.mute = object.value(QStringLiteral("mute")).toBool(false);
        settings.solo = object.value(QStringLiteral("solo")).toBool(false);
        if (!settings.trackId.isEmpty()) m_tracks.insert(settings.trackId, settings);
    }
    return true;
}

bool ProjectAudioSettings::write(const QString& path, QString* error) const
{
    QJsonObject root;
    root.insert(QStringLiteral("activeSoundProfile"), m_activeProfile);
    QJsonArray tracks;
    for (const auto& settings : m_tracks) {
        QJsonObject object;
        object.insert(QStringLiteral("trackId"), settings.trackId);
        object.insert(QStringLiteral("resourcePath"), settings.resource.path);
        object.insert(QStringLiteral("volume"), settings.volume);
        object.insert(QStringLiteral("pan"), settings.pan);
        object.insert(QStringLiteral("mute"), settings.mute);
        object.insert(QStringLiteral("solo"), settings.solo);
        tracks.append(object);
    }
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (error) *error = file.errorString();
        return false;
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    return true;
}

} // namespace midi_play::resources
