#include "playbackmodel.h"

namespace midi_play::playback {

PlaybackModel::PlaybackModel(std::shared_ptr<const music::MusicDocument> document)
    : m_document(std::move(document))
{
    if (!m_document) return;
    m_context = std::make_shared<PlaybackContext>(m_document);
    PlaybackEventsRenderer renderer(m_context);
    for (const auto& track : m_document->tracks()) {
        PlaybackData data;
        data.trackId = track.id;
        data.setupData.soundId = track.program == 0 ? QStringLiteral("piano") : QStringLiteral("midi-program-%1").arg(track.program);
        for (const auto& note : track.notes) {
            const qint64 timestamp = m_document->tickToMicroseconds(note.start);
            const qint64 end = m_document->tickToMicroseconds(note.start + note.duration);
            data.events.push_back({timestamp, qMax<qint64>(1'000, end - timestamp), note.channel, note.pitch,
                                   note.velocity, note.program});
        }
        data.events = renderer.render(data);
        data.index = std::make_shared<PlaybackEventIndex>(data.events);
        m_tracks.push_back(std::move(data));
    }
}

qint64 PlaybackModel::durationUs() const
{
    return m_document ? m_document->tickToMicroseconds(m_document->duration()) : 0;
}

const PlaybackData* PlaybackModel::track(const QString& id) const
{
    for (const auto& track : m_tracks) if (track.trackId == id) return &track;
    return nullptr;
}

} // namespace midi_play::playback
