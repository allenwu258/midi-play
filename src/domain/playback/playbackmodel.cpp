#include "playbackmodel.h"

namespace midi_play::playback {

PlaybackModel::PlaybackModel(std::shared_ptr<const music::MusicDocument> document)
    : m_document(std::move(document))
{
    if (!m_document) return;
    m_context = std::make_shared<PlaybackContext>(m_document);
    PlaybackEventsRenderer renderer(m_context);
    const auto segments = m_document->playbackSegments();
    for (const auto& track : m_document->tracks()) {
        QMap<QString, QVector<const music::NoteEvent*>> voiceGroups;
        for (const auto& note : track.notes) {
            const QString key = QStringLiteral("%1/staff%2/voice%3").arg(track.id).arg(note.staff).arg(note.voice);
            voiceGroups[key].push_back(&note);
        }
        for (auto group = voiceGroups.cbegin(); group != voiceGroups.cend(); ++group) {
            PlaybackData data;
            data.trackId = group.key();
            data.setupData.soundId = track.program == 0 ? QStringLiteral("piano") : QStringLiteral("midi-program-%1").arg(track.program);
        for (const auto& segment : segments) {
            for (const auto* note : group.value()) {
                if (note->start < segment.sourceStart || note->start >= segment.sourceEnd) continue;
                const music::Tick outputTick = segment.outputStart + (note->start - segment.sourceStart);
                const qint64 timestamp = m_document->tickToMicroseconds(outputTick);
                const qint64 end = m_document->tickToMicroseconds(outputTick + note->duration);
                data.events.push_back({timestamp, qMax<qint64>(1'000, end - timestamp), note->channel, note->pitch,
                                       note->velocity, note->program, note->tieStart, note->tieStop, note->staccato,
                                       note->accent, note->tenuto, note->ghost});
            }
            for (const auto& change : track.instrumentChanges) {
                if (change.tick < segment.sourceStart || change.tick >= segment.sourceEnd) continue;
                const music::Tick outputTick = segment.outputStart + (change.tick - segment.sourceStart);
                data.events.push_back({m_document->tickToMicroseconds(outputTick), 0, change.channel, 0, 0,
                                       change.program, false, false, false, false, false, false,
                                       PlaybackEvent::Kind::ProgramChange});
            }
        }
        data.events = renderer.render(data);
        data.index = std::make_shared<PlaybackEventIndex>(data.events);
        m_tracks.push_back(std::move(data));
        }
    }
}

qint64 PlaybackModel::durationUs() const
{
    return m_document ? m_document->tickToMicroseconds(m_document->playbackDuration()) : 0;
}

const PlaybackData* PlaybackModel::track(const QString& id) const
{
    for (const auto& track : m_tracks) if (track.trackId == id) return &track;
    return nullptr;
}

} // namespace midi_play::playback
