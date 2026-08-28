#include "playbackvisualizationprojector.h"

#include <QHash>
#include <QSet>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace midi_play::visualization {
namespace {

using music::PlaybackSegment;
using music::Tick;

struct SourceGridLine {
    Tick tick = 0;
    int measureNumber = 0;
    int beatIndex = 0;
    bool measureStart = false;
};

constexpr std::array<ColorRgba, 10> kTrackPalette {{
    {45, 201, 151, 255},
    {63, 169, 245, 255},
    {246, 190, 62, 255},
    {239, 101, 107, 255},
    {174, 111, 224, 255},
    {65, 199, 207, 255},
    {238, 137, 67, 255},
    {132, 191, 88, 255},
    {223, 102, 172, 255},
    {116, 139, 245, 255}
}};

template<typename T, typename TickGetter>
T valueAtTick(const QVector<T>& values, Tick tick, T fallback, TickGetter getter)
{
    for (const auto& value : values) {
        if (getter(value) > tick) break;
        fallback = value;
    }
    return fallback;
}

QVector<music::TimeSignatureChange> collectTimeSignatures(const music::MusicDocument& document)
{
    QVector<music::TimeSignatureChange> result;
    for (const auto& track : document.tracks()) result += track.timeSignatures;
    std::sort(result.begin(), result.end(), [](const auto& left, const auto& right) {
        return left.tick < right.tick;
    });
    QVector<music::TimeSignatureChange> deduplicated;
    for (const auto& value : result) {
        if (!deduplicated.isEmpty() && deduplicated.back().tick == value.tick) deduplicated.back() = value;
        else deduplicated.push_back(value);
    }
    if (deduplicated.isEmpty() || deduplicated.front().tick > 0) {
        deduplicated.push_front({0, 4, 4});
    }
    return deduplicated;
}

QVector<SourceGridLine> buildSourceGrid(const music::MusicDocument& document,
                                        const QVector<music::TimeSignatureChange>& signatures)
{
    QVector<SourceGridLine> result;
    const bool hasReaderMeasures = std::any_of(document.tracks().cbegin(), document.tracks().cend(),
                                               [](const auto& track) { return !track.measures.isEmpty(); });
    if (hasReaderMeasures && !document.measures().isEmpty()) {
        for (int measureIndex = 0; measureIndex < document.measures().size(); ++measureIndex) {
            const auto& measure = document.measures()[measureIndex];
            if (measure.duration <= 0) continue;
            const auto signature = valueAtTick(signatures, measure.start,
                                               music::TimeSignatureChange {0, 4, 4},
                                               [](const auto& value) { return value.tick; });
            const Tick beatTicks = std::max<Tick>(1, music::MusicDocument::kPpq * 4 / std::max(1, signature.beatType));
            int beat = 0;
            for (Tick tick = measure.start; tick < measure.start + measure.duration; tick += beatTicks) {
                result.push_back({tick, measure.number > 0 ? measure.number : measureIndex + 1,
                                  beat, beat == 0});
                ++beat;
            }
        }
        return result;
    }

    Tick cursor = 0;
    int measureNumber = 1;
    int safety = 0;
    while (cursor < document.duration() && safety++ < 1'000'000) {
        const auto signature = valueAtTick(signatures, cursor,
                                           music::TimeSignatureChange {0, 4, 4},
                                           [](const auto& value) { return value.tick; });
        const Tick beatTicks = std::max<Tick>(1, music::MusicDocument::kPpq * 4 / std::max(1, signature.beatType));
        Tick measureEnd = std::min(document.duration(), cursor + beatTicks * std::max(1, signature.beats));
        for (const auto& change : signatures) {
            if (change.tick > cursor && change.tick < measureEnd) {
                measureEnd = change.tick;
                break;
            }
        }
        int beat = 0;
        for (Tick tick = cursor; tick < measureEnd; tick += beatTicks) {
            result.push_back({tick, measureNumber, beat, beat == 0});
            ++beat;
        }
        if (measureEnd <= cursor) break;
        cursor = measureEnd;
        ++measureNumber;
    }
    return result;
}

qint64 projectedTimeUs(const music::MusicDocument& document,
                       const PlaybackSegment& segment, Tick sourceTick)
{
    const Tick outputTick = segment.outputStart + (sourceTick - segment.sourceStart);
    return document.playbackTickToMicroseconds(outputTick);
}

quint32 noteFlags(const music::NoteEvent& note, bool percussion)
{
    quint32 flags = NoNoteVisualFlags;
    if (note.grace) flags |= GraceNote;
    if (note.ghost) flags |= GhostNote;
    if (note.staccato) flags |= StaccatoNote;
    if (note.accent) flags |= AccentNote;
    if (note.tenuto) flags |= TenutoNote;
    if (note.marcato) flags |= MarcatoNote;
    if (note.tremolo) flags |= TremoloNote;
    if (note.tieStart || note.tieStop || note.tieGroupId != 0) flags |= TiedNote;
    if (percussion) flags |= PercussionNote;
    return flags;
}

QString simplifiedLabel(const music::NoteEvent& note)
{
    if (note.scaleDegree < 1 || note.scaleDegree > 7) return {};
    QString prefix;
    if (note.accidental < 0) prefix = QString(-note.accidental, QLatin1Char('b'));
    else if (note.accidental > 0) prefix = QString(note.accidental, QLatin1Char('#'));
    return prefix + QString::number(note.scaleDegree);
}

Tick pedalExtendedEnd(const music::Track& track, Tick noteStart, Tick keyEnd, Tick segmentEnd)
{
    auto extendForController = [&](int controller, bool requirePressDuringNote) {
        int value = 0;
        Tick lastPress = -1;
        for (const auto& change : track.controlChanges) {
            if (change.tick > keyEnd) break;
            if (change.controller != controller) continue;
            value = change.value;
            if (value >= 64) lastPress = change.tick;
        }
        if (value < 64 || (requirePressDuringNote && lastPress < noteStart)) return keyEnd;
        for (const auto& change : track.controlChanges) {
            if (change.controller == controller && change.tick > keyEnd && change.value < 64) {
                return std::min(change.tick, segmentEnd);
            }
        }
        return segmentEnd;
    };

    Tick end = extendForController(64, false);
    end = std::max(end, extendForController(66, true));
    return std::clamp(end, keyEnd, segmentEnd);
}

int floorToC(int pitch)
{
    const int pitchClass = ((pitch % 12) + 12) % 12;
    return pitch - pitchClass;
}

int ceilToB(int pitch)
{
    return floorToC(pitch) + 11;
}

template<typename T, typename TimeGetter>
void sortAndKeepLastAtSameTime(QVector<T>& values, TimeGetter getter)
{
    std::stable_sort(values.begin(), values.end(), [&](const auto& left, const auto& right) {
        return getter(left) < getter(right);
    });
    QVector<T> result;
    result.reserve(values.size());
    for (const auto& value : values) {
        if (!result.isEmpty() && getter(result.back()) == getter(value)) result.back() = value;
        else result.push_back(value);
    }
    values = std::move(result);
}

} // namespace

VisualChartPtr PlaybackVisualizationProjector::project(const music::MusicDocument& document,
                                                       quint64 generation,
                                                       const VisualizationProjectionOptions& options,
                                                       QString* error) const
{
    if (!document.isValid()) {
        if (error) *error = QStringLiteral("无法为无效音乐文档建立播放可视化");
        return {};
    }

    auto chart = std::make_shared<VisualChart>();
    chart->m_title = document.title().isEmpty() ? options.fallbackTitle : document.title();
    chart->m_durationUs = document.playbackTickToMicroseconds(document.playbackDuration());
    chart->m_generation = generation;

    QHash<int, QString> drumNames;
    QSet<int> drumPitches;
    for (const auto& track : document.tracks()) {
        if (!track.percussion) continue;
        for (const auto& entry : track.drumMap) {
            drumPitches.insert(entry.pitch);
            if (!entry.name.isEmpty()) drumNames.insert(entry.pitch, entry.name);
        }
        for (const auto& note : track.notes) drumPitches.insert(note.pitch);
    }
    QVector<int> sortedDrumPitches(drumPitches.cbegin(), drumPitches.cend());
    std::sort(sortedDrumPitches.begin(), sortedDrumPitches.end());
    QHash<int, int> drumLaneForPitch;
    for (int i = 0; i < sortedDrumPitches.size(); ++i) {
        const int pitch = sortedDrumPitches[i];
        drumLaneForPitch.insert(pitch, i);
        chart->m_drumLanes.push_back({i, pitch, drumNames.value(pitch, QStringLiteral("GM %1").arg(pitch))});
    }

    chart->m_tracks.reserve(document.tracks().size());
    for (int trackIndex = 0; trackIndex < document.tracks().size(); ++trackIndex) {
        const auto& source = document.tracks()[trackIndex];
        VisualTrack track;
        track.id = source.id;
        track.name = source.name.isEmpty() ? QStringLiteral("Track %1").arg(trackIndex + 1) : source.name;
        track.color = kTrackPalette[trackIndex % kTrackPalette.size()];
        track.percussion = source.percussion;
        track.channel = source.channel;
        track.program = source.program;
        chart->m_tracks.push_back(std::move(track));
    }

    const auto segments = document.playbackSegments();
    QVector<VisualNote> projectedNotes;
    for (int segmentIndex = 0; segmentIndex < segments.size(); ++segmentIndex) {
        const auto& segment = segments[segmentIndex];
        for (int trackIndex = 0; trackIndex < document.tracks().size(); ++trackIndex) {
            const auto& track = document.tracks()[trackIndex];
            for (const auto& note : track.notes) {
                if (note.rest || note.start < segment.sourceStart || note.start >= segment.sourceEnd) continue;
                const Tick availableDuration = std::max<Tick>(1, segment.sourceEnd - note.start);
                const Tick sourceDuration = std::max<Tick>(1, std::min(note.duration, availableDuration));
                double durationFactor = 1.0;
                if (note.staccato) durationFactor *= 0.5;
                if (note.tenuto) durationFactor *= 0.98;
                const Tick keyDuration = std::clamp<Tick>(
                    static_cast<Tick>(std::llround(sourceDuration * durationFactor)), 1, availableDuration);
                const Tick keyEnd = note.start + keyDuration;
                const Tick audibleEnd = pedalExtendedEnd(track, note.start, keyEnd, segment.sourceEnd);

                VisualNote visual;
                visual.sourceNoteId = note.noteId;
                visual.trackIndex = trackIndex;
                visual.pitch = note.pitch;
                visual.velocity = note.velocity;
                visual.channel = note.channel;
                visual.staff = note.staff;
                visual.voice = note.voice;
                visual.sourceMeasureIndex = segment.sourceMeasureIndex >= 0
                    ? segment.sourceMeasureIndex : note.measureIndex;
                visual.repeatPass = segment.repeatPass;
                visual.scaleDegree = note.scaleDegree;
                visual.accidental = note.accidental;
                visual.octaveOffset = note.writtenPitch.octave - 4;
                visual.drumLane = track.percussion ? drumLaneForPitch.value(note.pitch, -1) : -1;
                visual.startUs = projectedTimeUs(document, segment, note.start);
                visual.keyEndUs = projectedTimeUs(document, segment, keyEnd);
                visual.audibleEndUs = projectedTimeUs(document, segment, audibleEnd);
                visual.sourceChordId = note.chordId;
                visual.chordInstanceId = (static_cast<quint64>(segmentIndex + 1) << 40)
                    ^ (static_cast<quint64>(trackIndex + 1) << 32) ^ note.chordId;
                visual.tieGroupId = note.tieGroupId;
                visual.flags = noteFlags(note, track.percussion);
                if (!track.percussion) visual.simplifiedLabel = simplifiedLabel(note);
                projectedNotes.push_back(std::move(visual));
            }
        }
    }

    std::stable_sort(projectedNotes.begin(), projectedNotes.end(), [](const auto& left, const auto& right) {
        if (left.startUs != right.startUs) return left.startUs < right.startUs;
        if (left.pitch != right.pitch) return left.pitch < right.pitch;
        if (left.trackIndex != right.trackIndex) return left.trackIndex < right.trackIndex;
        return left.sourceNoteId < right.sourceNoteId;
    });

    QHash<QString, int> lastTieIndex;
    chart->m_notes.reserve(projectedNotes.size());
    for (auto& note : projectedNotes) {
        if (options.mergeTies && note.tieGroupId != 0) {
            const QString key = QStringLiteral("%1/%2/%3/%4/%5")
                .arg(note.trackIndex).arg(note.tieGroupId).arg(note.pitch).arg(note.staff).arg(note.voice);
            const int previousIndex = lastTieIndex.value(key, -1);
            if (previousIndex >= 0) {
                auto& previous = chart->m_notes[previousIndex];
                if (note.startUs >= previous.startUs && note.startUs <= previous.audibleEndUs + 2'000) {
                    previous.keyEndUs = std::max(previous.keyEndUs, note.keyEndUs);
                    previous.audibleEndUs = std::max(previous.audibleEndUs, note.audibleEndUs);
                    previous.flags |= note.flags;
                    continue;
                }
            }
            lastTieIndex.insert(key, chart->m_notes.size());
        }
        chart->m_notes.push_back(std::move(note));
    }

    int minimumPitch = 127;
    int maximumPitch = 0;
    quint64 instanceId = 1;
    for (int begin = 0; begin < chart->m_notes.size();) {
        int end = begin + 1;
        while (end < chart->m_notes.size()
               && chart->m_notes[end].startUs == chart->m_notes[begin].startUs
               && chart->m_notes[end].pitch == chart->m_notes[begin].pitch
               && chart->m_notes[end].isPercussion() == chart->m_notes[begin].isPercussion()) {
            ++end;
        }
        const int count = end - begin;
        for (int index = begin; index < end; ++index) {
            chart->m_notes[index].coincidentIndex = index - begin;
            chart->m_notes[index].coincidentCount = count;
        }
        begin = end;
    }
    for (int index = 0; index < chart->m_notes.size(); ++index) {
        auto& note = chart->m_notes[index];
        note.instanceId = instanceId++;
        chart->m_tracks[note.trackIndex].noteIndices.push_back(index);
        if (!note.isPercussion()) {
            minimumPitch = std::min(minimumPitch, note.pitch);
            maximumPitch = std::max(maximumPitch, note.pitch);
        }
    }

    if (minimumPitch > maximumPitch) {
        minimumPitch = 48;
        maximumPitch = 71;
    } else {
        minimumPitch = floorToC(minimumPitch - std::max(0, options.pitchGuardSemitones));
        maximumPitch = ceilToB(maximumPitch + std::max(0, options.pitchGuardSemitones));
        const int requestedSpan = std::max(12, options.minimumPitchSpan);
        while (maximumPitch - minimumPitch + 1 < requestedSpan) {
            minimumPitch -= 6;
            maximumPitch += 6;
        }
        minimumPitch = floorToC(minimumPitch);
        maximumPitch = ceilToB(maximumPitch);
    }
    if (minimumPitch < 0) {
        maximumPitch = std::min(127, maximumPitch - minimumPitch);
        minimumPitch = 0;
    }
    if (maximumPitch > 127) {
        minimumPitch = std::max(0, minimumPitch - (maximumPitch - 127));
        maximumPitch = 127;
    }
    chart->m_pitchRange = {minimumPitch, maximumPitch};

    chart->m_noteIndicesByEnd.resize(chart->m_notes.size());
    for (int i = 0; i < chart->m_noteIndicesByEnd.size(); ++i) chart->m_noteIndicesByEnd[i] = i;
    std::sort(chart->m_noteIndicesByEnd.begin(), chart->m_noteIndicesByEnd.end(), [&](int left, int right) {
        if (chart->m_notes[left].audibleEndUs != chart->m_notes[right].audibleEndUs) {
            return chart->m_notes[left].audibleEndUs < chart->m_notes[right].audibleEndUs;
        }
        return chart->m_notes[left].startUs < chart->m_notes[right].startUs;
    });

    const auto signatures = collectTimeSignatures(document);
    const auto sourceGrid = buildSourceGrid(document, signatures);
    for (const auto& segment : segments) {
        for (const auto& line : sourceGrid) {
            if (line.tick < segment.sourceStart || line.tick >= segment.sourceEnd) continue;
            chart->m_gridLines.push_back({
                projectedTimeUs(document, segment, line.tick),
                line.measureNumber,
                line.beatIndex,
                line.measureStart,
                line.measureStart ? QStringLiteral("M%1").arg(line.measureNumber) : QString {}
            });
        }
    }
    std::sort(chart->m_gridLines.begin(), chart->m_gridLines.end(), [](const auto& left, const auto& right) {
        return left.timeUs < right.timeUs;
    });

    QVector<music::TempoChange> tempos = document.tempos();
    std::sort(tempos.begin(), tempos.end(), [](const auto& left, const auto& right) { return left.tick < right.tick; });
    if (tempos.isEmpty() || tempos.front().tick > 0) tempos.push_front({0, 120.0, 0});
    QVector<music::KeySignatureChange> keys = document.keySignatures();
    std::sort(keys.begin(), keys.end(), [](const auto& left, const auto& right) { return left.tick < right.tick; });
    if (keys.isEmpty() || keys.front().tick > 0) {
        keys.push_front({0, document.keyContext().fifths, document.keyContext().mode});
    }

    for (const auto& segment : segments) {
        const auto tempo = valueAtTick(tempos, segment.sourceStart, music::TempoChange {0, 120.0, 0},
                                       [](const auto& value) { return value.tick; });
        chart->m_tempos.push_back({projectedTimeUs(document, segment, segment.sourceStart), tempo.bpm});
        for (const auto& change : tempos) {
            if (change.tick > segment.sourceStart && change.tick < segment.sourceEnd) {
                chart->m_tempos.push_back({projectedTimeUs(document, segment, change.tick), change.bpm});
            }
        }

        const auto key = valueAtTick(keys, segment.sourceStart,
                                    music::KeySignatureChange {0, 0, QStringLiteral("major")},
                                    [](const auto& value) { return value.tick; });
        chart->m_keys.push_back({projectedTimeUs(document, segment, segment.sourceStart), key.fifths, key.mode});
        for (const auto& change : keys) {
            if (change.tick > segment.sourceStart && change.tick < segment.sourceEnd) {
                chart->m_keys.push_back({projectedTimeUs(document, segment, change.tick),
                                         change.fifths, change.mode});
            }
        }

        const auto signature = valueAtTick(signatures, segment.sourceStart,
                                           music::TimeSignatureChange {0, 4, 4},
                                           [](const auto& value) { return value.tick; });
        chart->m_timeSignatures.push_back({projectedTimeUs(document, segment, segment.sourceStart),
                                           signature.beats, signature.beatType});
        for (const auto& change : signatures) {
            if (change.tick > segment.sourceStart && change.tick < segment.sourceEnd) {
                chart->m_timeSignatures.push_back({projectedTimeUs(document, segment, change.tick),
                                                   change.beats, change.beatType});
            }
        }

        for (const auto& marker : document.markers()) {
            if (marker.tick < segment.sourceStart || marker.tick >= segment.sourceEnd) continue;
            chart->m_markers.push_back({instanceId++, projectedTimeUs(document, segment, marker.tick),
                                        marker.text, MarkerKind::Text});
        }
        for (const auto& lyric : document.lyrics()) {
            if (lyric.tick < segment.sourceStart || lyric.tick >= segment.sourceEnd) continue;
            chart->m_lyrics.push_back({instanceId++, projectedTimeUs(document, segment, lyric.tick),
                                       lyric.text, lyric.verse});
        }
    }

    sortAndKeepLastAtSameTime(chart->m_tempos, [](const auto& point) { return point.timeUs; });
    sortAndKeepLastAtSameTime(chart->m_keys, [](const auto& point) { return point.timeUs; });
    sortAndKeepLastAtSameTime(chart->m_timeSignatures, [](const auto& point) { return point.timeUs; });
    std::stable_sort(chart->m_markers.begin(), chart->m_markers.end(), [](const auto& left, const auto& right) {
        return left.timeUs < right.timeUs;
    });
    std::stable_sort(chart->m_lyrics.begin(), chart->m_lyrics.end(), [](const auto& left, const auto& right) {
        return left.timeUs < right.timeUs;
    });

    if (error) error->clear();
    return chart;
}

} // namespace midi_play::visualization
