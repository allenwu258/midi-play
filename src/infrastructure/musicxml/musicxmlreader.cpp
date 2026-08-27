#include "musicxmlreader.h"
#include "domain/music/musicanalysis.h"

#include <QFile>
#include <QFileInfo>
#include <QBuffer>
#include <QDomDocument>
#include <QXmlStreamReader>
#include <QMap>

#include <algorithm>

namespace midi_play::musicxml {
namespace {

QByteArray convertTimewiseToPartwise(const QByteArray& input, QString* error)
{
    QDomDocument source;
    QString parseError;
    int line = 0;
    int column = 0;
    if (!source.setContent(input, &parseError, &line, &column)) {
        if (error) *error = QStringLiteral("MusicXML XML 无效 (%1:%2): %3")
            .arg(line).arg(column).arg(parseError);
        return {};
    }
    const QDomElement sourceRoot = source.documentElement();
    if (sourceRoot.tagName() != QStringLiteral("score-timewise")) return input;

    QDomDocument result;
    QDomElement root = result.createElement(QStringLiteral("score-partwise"));
    const auto rootAttributes = sourceRoot.attributes();
    for (int i = 0; i < rootAttributes.size(); ++i) {
        const auto attribute = rootAttributes.item(i).toAttr();
        root.setAttribute(attribute.name(), attribute.value());
    }
    result.appendChild(root);

    QMap<QString, QDomElement> parts;
    for (QDomNode child = sourceRoot.firstChild(); !child.isNull(); child = child.nextSibling()) {
        const auto element = child.toElement();
        if (element.isNull()) continue;
        if (element.tagName() == QStringLiteral("part-list")) {
            root.appendChild(element.cloneNode(true));
            for (QDomNode partNode = element.firstChild(); !partNode.isNull(); partNode = partNode.nextSibling()) {
                const auto scorePart = partNode.toElement();
                if (scorePart.isNull() || scorePart.tagName() != QStringLiteral("score-part")) continue;
                const QString id = scorePart.attribute(QStringLiteral("id"));
                if (id.isEmpty()) continue;
                auto part = result.createElement(QStringLiteral("part"));
                part.setAttribute(QStringLiteral("id"), id);
                root.appendChild(part);
                parts.insert(id, part);
            }
        } else if (element.tagName() != QStringLiteral("measure")) {
            root.appendChild(element.cloneNode(true));
        }
    }

    for (QDomNode measureNode = sourceRoot.firstChild(); !measureNode.isNull(); measureNode = measureNode.nextSibling()) {
        const auto sourceMeasure = measureNode.toElement();
        if (sourceMeasure.isNull() || sourceMeasure.tagName() != QStringLiteral("measure")) continue;
        const auto measureAttributes = sourceMeasure.attributes();
        for (auto partIt = parts.begin(); partIt != parts.end(); ++partIt) {
            auto targetMeasure = result.createElement(QStringLiteral("measure"));
            for (int i = 0; i < measureAttributes.size(); ++i) {
                const auto attribute = measureAttributes.item(i).toAttr();
                targetMeasure.setAttribute(attribute.name(), attribute.value());
            }
            for (QDomNode partNode = sourceMeasure.firstChild(); !partNode.isNull(); partNode = partNode.nextSibling()) {
                const auto sourcePart = partNode.toElement();
                if (sourcePart.isNull() || sourcePart.tagName() != QStringLiteral("part")
                    || sourcePart.attribute(QStringLiteral("id")) != partIt.key()) continue;
                for (QDomNode content = sourcePart.firstChild(); !content.isNull(); content = content.nextSibling()) {
                    targetMeasure.appendChild(content.cloneNode(true));
                }
                break;
            }
            partIt.value().appendChild(targetMeasure);
        }
    }
    return result.toByteArray(2);
}

struct InstrumentInfo {
    QString name;
    int program = 0;
    int channel = 0;
    bool percussion = false;
};

int stepToPitch(const QString& step)
{
    static const QHash<QString, int> values {{QStringLiteral("C"), 0}, {QStringLiteral("D"), 2},
                                             {QStringLiteral("E"), 4}, {QStringLiteral("F"), 5},
                                             {QStringLiteral("G"), 7}, {QStringLiteral("A"), 9},
                                             {QStringLiteral("B"), 11}};
    return values.value(step.toUpper(), 0);
}

int stepToIndex(const QString& step)
{
    static const QHash<QString, int> values {{QStringLiteral("C"), 0}, {QStringLiteral("D"), 1},
                                             {QStringLiteral("E"), 2}, {QStringLiteral("F"), 3},
                                             {QStringLiteral("G"), 4}, {QStringLiteral("A"), 5},
                                             {QStringLiteral("B"), 6}};
    return values.value(step.toUpper(), 0);
}

int parseProgram(QXmlStreamReader& xml)
{
    int program = 0;
    while (xml.readNextStartElement()) {
        if (xml.name() == u"midi-program") {
            program = std::clamp(xml.readElementText().toInt() - 1, 0, 127);
        } else {
            xml.skipCurrentElement();
        }
    }
    return program;
}

void parseInstrument(QXmlStreamReader& xml, QHash<QString, InstrumentInfo>& instruments)
{
    const QString id = xml.attributes().value(u"id").toString();
    InstrumentInfo info;
    while (xml.readNextStartElement()) {
        if (xml.name() == u"instrument-name") {
            info.name = xml.readElementText();
            info.percussion = info.name.contains(QStringLiteral("drum"), Qt::CaseInsensitive)
                || info.name.contains(QStringLiteral("percussion"), Qt::CaseInsensitive);
        } else if (xml.name() == u"midi-instrument") {
            info.program = parseProgram(xml);
        } else {
            xml.skipCurrentElement();
        }
    }
    instruments.insert(id, info);
}

void parseScorePart(QXmlStreamReader& xml, QHash<QString, InstrumentInfo>& instruments,
                    QHash<QString, QString>& partNames, QHash<QString, InstrumentInfo>& partInstruments)
{
    const QString id = xml.attributes().value(u"id").toString();
    QString name;
    QString firstInstrumentId;
    while (xml.readNextStartElement()) {
        if (xml.name() == u"part-name") {
            name = xml.readElementText();
        } else if (xml.name() == u"score-instrument") {
            firstInstrumentId = xml.attributes().value(u"id").toString();
            parseInstrument(xml, instruments);
        } else if (xml.name() == u"midi-instrument") {
            const QString instrumentId = xml.attributes().value(u"id").toString();
            InstrumentInfo info = instruments.value(instrumentId);
            while (xml.readNextStartElement()) {
                if (xml.name() == u"midi-channel") {
                    info.channel = std::clamp(xml.readElementText().toInt() - 1, 0, 15);
                } else if (xml.name() == u"midi-program") {
                    info.program = std::clamp(xml.readElementText().toInt() - 1, 0, 127);
                } else {
                    xml.skipCurrentElement();
                }
            }
            instruments.insert(instrumentId, info);
        } else {
            xml.skipCurrentElement();
        }
    }
    partNames.insert(id, name);
    if (!firstInstrumentId.isEmpty()) partInstruments.insert(id, instruments.value(firstInstrumentId));
}

int parsePitch(QXmlStreamReader& xml)
{
    QString step;
    int alter = 0;
    int octave = 4;
    while (xml.readNextStartElement()) {
        if (xml.name() == u"step") step = xml.readElementText();
        else if (xml.name() == u"alter") alter = xml.readElementText().toInt();
        else if (xml.name() == u"octave") octave = xml.readElementText().toInt();
        else xml.skipCurrentElement();
    }
    return std::clamp((octave + 1) * 12 + stepToPitch(step) + alter, 0, 127);
}

int parseDivisions(QXmlStreamReader& xml)
{
    int divisions = 1;
    while (xml.readNextStartElement()) {
        if (xml.name() == u"divisions") divisions = std::max(1, xml.readElementText().toInt());
        else xml.skipCurrentElement();
    }
    return divisions;
}

int dynamicVelocity(const QString& name)
{
    const QString value = name.toLower();
    if (value == u"pppp") return 24;
    if (value == u"ppp") return 34;
    if (value == u"pp") return 44;
    if (value == u"p") return 58;
    if (value == u"mp") return 72;
    if (value == u"mf") return 88;
    if (value == u"f") return 104;
    if (value == u"ff") return 116;
    if (value == u"fff") return 124;
    if (value == u"ffff") return 127;
    return 90;
}

void parseDirection(QXmlStreamReader& xml, music::MusicDocument& document, music::Track& track,
                    music::Measure& measure, music::Tick tick)
{
    double bpm = -1;
    int velocity = -1;
    while (xml.readNextStartElement()) {
        if (xml.name() == u"sound") {
            const auto value = xml.attributes().value(u"tempo");
            if (!value.isEmpty()) bpm = value.toDouble();
            const auto dynamics = xml.attributes().value(u"dynamics");
            if (!dynamics.isEmpty()) velocity = std::clamp(dynamics.toInt(), 1, 127);
            measure.daCapo = xml.attributes().value(u"dacapo") == u"yes";
            measure.fine = xml.attributes().value(u"fine") == u"yes";
            measure.toCoda = !xml.attributes().value(u"tocoda").isEmpty();
            measure.dalSegno = !xml.attributes().value(u"dalsegno").isEmpty();
            measure.segno = measure.segno || !xml.attributes().value(u"segno").isEmpty();
            measure.coda = measure.coda || !xml.attributes().value(u"coda").isEmpty();
            xml.skipCurrentElement();
        } else if (xml.name() == u"direction-type") {
            while (xml.readNextStartElement()) {
                if (xml.name() == u"metronome") {
                    while (xml.readNextStartElement()) {
                        if (xml.name() == u"per-minute") bpm = xml.readElementText().toDouble();
                        else xml.skipCurrentElement();
                    }
                } else if (xml.name() == u"dynamics") {
                    while (xml.readNextStartElement()) {
                        velocity = dynamicVelocity(xml.name().toString());
                        xml.skipCurrentElement();
                    }
                } else if (xml.name() == u"words") {
                    const QString text = xml.readElementText().trimmed();
                    if (!text.isEmpty()) document.markers().push_back({tick, text,
                                                                         static_cast<quint64>(document.markers().size())});
                } else if (xml.name() == u"segno") {
                    measure.segno = true;
                    xml.skipCurrentElement();
                } else if (xml.name() == u"coda") {
                    measure.coda = true;
                    xml.skipCurrentElement();
                } else if (xml.name() == u"pedal") {
                    const auto type = xml.attributes().value(u"type");
                    const int value = (type == u"start" || type == u"change") ? 127 : 0;
                    track.controlChanges.push_back({tick, track.channel, 64, value});
                    xml.skipCurrentElement();
                } else if (xml.name() == u"wedge") {
                    const auto type = xml.attributes().value(u"type");
                    if (type == u"stop") {
                        for (int index = track.hairpins.size() - 1; index >= 0; --index) {
                            if (track.hairpins[index].end == 0) {
                                track.hairpins[index].end = tick;
                                break;
                            }
                        }
                    } else if (type == u"crescendo" || type == u"diminuendo") {
                        track.hairpins.push_back({tick, 0, type == u"crescendo"});
                    }
                    xml.skipCurrentElement();
                } else xml.skipCurrentElement();
            }
        } else xml.skipCurrentElement();
    }
    if (bpm > 0.0) document.tempos().push_back({tick, bpm});
    if (velocity > 0) track.dynamics.push_back({tick, velocity});
}

void parseNotations(QXmlStreamReader& xml, bool& tieStart, bool& tieStop,
                    bool& staccato, bool& accent, bool& tenuto,
                    bool& marcato, bool& tremolo)
{
    while (xml.readNextStartElement()) {
        if (xml.name() == u"tied") {
            const auto type = xml.attributes().value(u"type");
            tieStart = tieStart || type == u"start";
            tieStop = tieStop || type == u"stop";
            xml.skipCurrentElement();
        } else if (xml.name() == u"articulations") {
            while (xml.readNextStartElement()) {
                staccato = staccato || xml.name() == u"staccato";
                accent = accent || xml.name() == u"accent";
                tenuto = tenuto || xml.name() == u"tenuto";
                marcato = marcato || xml.name() == u"strong-accent";
                tremolo = tremolo || xml.name() == u"tremolo";
                xml.skipCurrentElement();
            }
        } else if (xml.name() == u"ornaments") {
            while (xml.readNextStartElement()) {
                tremolo = tremolo || xml.name() == u"tremolo";
                xml.skipCurrentElement();
            }
        } else {
            xml.skipCurrentElement();
        }
    }
}

void parseBarline(QXmlStreamReader& xml, music::Measure& measure)
{
    const auto location = xml.attributes().value(u"location");
    Q_UNUSED(location)
    while (xml.readNextStartElement()) {
        if (xml.name() == u"repeat") {
            const auto direction = xml.attributes().value(u"direction");
            if (direction == u"forward") measure.repeatStart = true;
            if (direction == u"backward") {
                measure.repeatEnd = true;
                bool ok = false;
                const int count = xml.attributes().value(u"times").toInt(&ok);
                if (ok && count > 0) measure.repeatCount = count;
            }
            xml.skipCurrentElement();
        } else if (xml.name() == u"ending") {
            bool ok = false;
            measure.endingNumber = xml.attributes().value(u"number").toInt(&ok);
            measure.endingType = xml.attributes().value(u"type").toString();
            xml.skipCurrentElement();
        } else {
            xml.skipCurrentElement();
        }
    }
}

struct ParsedNote {
    QString step;
    int alter = 0;
    int octave = 4;
    int durationValue = 0;
    int voice = 1;
    int staff = 1;
    bool rest = false;
    bool unpitched = false;
    bool chord = false;
    bool grace = false;
    int velocity = 90;
    bool tieStart = false;
    bool tieStop = false;
    bool staccato = false;
    bool accent = false;
    bool tenuto = false;
    bool marcato = false;
    bool tremolo = false;
    QString instrumentId;
    QString lyric;
    int lyricVerse = 1;
};

ParsedNote parseNote(QXmlStreamReader& xml, int defaultStaff)
{
    ParsedNote note;
    note.staff = defaultStaff;
    if (xml.attributes().hasAttribute(u"dynamics")) {
        note.velocity = std::clamp(xml.attributes().value(u"dynamics").toInt(), 1, 127);
    }
    while (xml.readNextStartElement()) {
        if (xml.name() == u"pitch" || xml.name() == u"unpitched") {
            note.unpitched = xml.name() == u"unpitched";
            while (xml.readNextStartElement()) {
                if (xml.name() == u"step") note.step = xml.readElementText();
                else if (xml.name() == u"alter") note.alter = xml.readElementText().toInt();
                else if (xml.name() == u"octave") note.octave = xml.readElementText().toInt();
                else xml.skipCurrentElement();
            }
        } else if (xml.name() == u"rest") { note.rest = true; xml.skipCurrentElement(); }
        else if (xml.name() == u"chord") { note.chord = true; xml.skipCurrentElement(); }
        else if (xml.name() == u"grace") { note.grace = true; xml.skipCurrentElement(); }
        else if (xml.name() == u"duration") note.durationValue = xml.readElementText().toInt();
        else if (xml.name() == u"voice") note.voice = std::max(1, xml.readElementText().toInt());
        else if (xml.name() == u"staff") note.staff = std::max(1, xml.readElementText().toInt());
        else if (xml.name() == u"instrument") note.instrumentId = xml.attributes().value(u"id").toString(), xml.skipCurrentElement();
        else if (xml.name() == u"lyric") {
            bool ok = false;
            note.lyricVerse = std::max(1, xml.attributes().value(u"number").toInt(&ok));
            while (xml.readNextStartElement()) {
                if (xml.name() == u"text") note.lyric = xml.readElementText();
                else xml.skipCurrentElement();
            }
        }
        else if (xml.name() == u"dynamics") note.velocity = std::clamp(xml.readElementText().toInt(), 1, 127);
        else if (xml.name() == u"tie") {
            const auto type = xml.attributes().value(u"type");
            note.tieStart = note.tieStart || type == u"start";
            note.tieStop = note.tieStop || type == u"stop";
            xml.skipCurrentElement();
        } else if (xml.name() == u"notations") {
            parseNotations(xml, note.tieStart, note.tieStop, note.staccato, note.accent,
                           note.tenuto, note.marcato, note.tremolo);
        }
        else xml.skipCurrentElement();
    }
    return note;
}

music::Tick parseMeasure(QXmlStreamReader& xml, music::Track& track, music::MusicDocument& document,
                         music::Tick measureStart, int& divisions, int measureNumber,
                         const QHash<QString, InstrumentInfo>& instruments, QString& currentInstrument,
                         QHash<QString, qint64>& durationRemainders)
{
    music::Tick cursor = measureStart;
    music::Tick measureEnd = measureStart;
    struct VoiceCursor { music::Tick tick = 0; music::Tick previousStart = 0; qint64 remainder = 0; };
    QHash<QString, VoiceCursor> voiceCursors;
    QString activeVoiceKey = QStringLiteral("1/1");
    voiceCursors.insert(activeVoiceKey, {measureStart, measureStart, 0});
    music::Measure measureInfo;
    measureInfo.number = measureNumber;
    measureInfo.start = measureStart;
    while (xml.readNextStartElement()) {
        if (xml.name() == u"attributes") {
            while (xml.readNextStartElement()) {
                if (xml.name() == u"divisions") {
                    const int nextDivisions = std::max(1, xml.readElementText().toInt());
                    if (nextDivisions != divisions) durationRemainders.clear();
                    divisions = nextDivisions;
                }
                else if (xml.name() == u"time") {
                    int beats = 4;
                    int beatType = 4;
                    while (xml.readNextStartElement()) {
                        if (xml.name() == u"beats") beats = std::max(1, xml.readElementText().toInt());
                        else if (xml.name() == u"beat-type") beatType = std::max(1, xml.readElementText().toInt());
                        else xml.skipCurrentElement();
                    }
                    track.timeSignatures.push_back({measureStart, beats, beatType});
                } else if (xml.name() == u"key") {
                    int fifths = 0;
                    QString mode = QStringLiteral("major");
                    while (xml.readNextStartElement()) {
                        if (xml.name() == u"fifths") fifths = xml.readElementText().toInt();
                        else if (xml.name() == u"mode") mode = xml.readElementText();
                        else xml.skipCurrentElement();
                    }
                    track.keySignatures.push_back({measureStart, fifths, mode});
                } else if (xml.name() == u"clef") {
                    bool ok = false;
                    const int staff = std::max(1, xml.attributes().value(u"number").toInt(&ok));
                    int actualStaff = ok ? staff : 1;
                    QString sign = QStringLiteral("G");
                    int line = 2;
                    int octaveChange = 0;
                    while (xml.readNextStartElement()) {
                        if (xml.name() == u"sign") sign = xml.readElementText();
                        else if (xml.name() == u"line") line = xml.readElementText().toInt();
                        else if (xml.name() == u"clef-octave-change") octaveChange = xml.readElementText().toInt();
                        else xml.skipCurrentElement();
                    }
                    track.clefs.push_back({measureStart, actualStaff, sign, line, octaveChange});
                } else {
                    xml.skipCurrentElement();
                }
            }
        } else if (xml.name() == u"note") {
            const ParsedNote parsed = parseNote(xml, 1);
            activeVoiceKey = QStringLiteral("%1/%2").arg(parsed.staff).arg(parsed.voice);
            auto& state = voiceCursors[activeVoiceKey];
            state.remainder = durationRemainders.value(activeVoiceKey, 0);
            if (state.tick == 0 && measureStart != 0) state.tick = measureStart;
            const qint64 scaled = static_cast<qint64>(parsed.durationValue) * music::MusicDocument::kPpq + state.remainder;
            const music::Tick duration = std::max<music::Tick>(1, scaled / std::max(1, divisions));
            if (!parsed.chord && !parsed.grace) {
                state.remainder = scaled % std::max(1, divisions);
                durationRemainders.insert(activeVoiceKey, state.remainder);
            }
            const music::Tick noteStart = parsed.chord ? state.previousStart : state.tick;
            if (!parsed.rest && !parsed.grace && (!parsed.step.isEmpty() || parsed.unpitched)) {
                music::NoteEvent event;
                event.start = noteStart;
                event.duration = duration;
                event.pitch = std::clamp((parsed.octave + 1) * 12 + stepToPitch(parsed.step) + parsed.alter, 0, 127);
                event.velocity = parsed.velocity;
                event.channel = track.channel;
                event.program = track.program;
                event.voice = parsed.voice;
                event.staff = parsed.staff;
                event.tieStart = parsed.tieStart;
                event.tieStop = parsed.tieStop;
                event.staccato = parsed.staccato;
                event.accent = parsed.accent;
                event.tenuto = parsed.tenuto;
                event.marcato = parsed.marcato;
                event.tremolo = parsed.tremolo;
                event.sequence = static_cast<quint64>(track.notes.size() + 1);
                event.writtenPitch = {stepToIndex(parsed.step), parsed.alter, parsed.octave, event.pitch};
                event.hasWrittenPitch = true;
                track.notes.push_back(event);
                state.previousStart = noteStart;
            }
            if (!parsed.chord && !parsed.grace) state.tick += duration;
            cursor = std::max(cursor, state.tick);
            QString noteInstrument = parsed.instrumentId;
            if (!noteInstrument.isEmpty() && noteInstrument != currentInstrument) {
                currentInstrument = noteInstrument;
                const auto info = instruments.value(noteInstrument);
                track.instrumentChanges.push_back({noteStart, info.channel, info.program, noteInstrument});
            }
            if (!parsed.lyric.isEmpty()) track.lyrics.push_back({noteStart, parsed.lyric,
                                                                  parsed.lyricVerse,
                                                                  static_cast<quint64>(track.lyrics.size())});
            measureEnd = std::max(measureEnd, state.tick);
        } else if (xml.name() == u"forward") {
            while (xml.readNextStartElement()) {
                if (xml.name() == u"duration") {
                    const qint64 value = xml.readElementText().toLongLong();
                    auto& state = voiceCursors[activeVoiceKey];
                    state.tick += std::max<qint64>(1, value * music::MusicDocument::kPpq / std::max(1, divisions));
                    cursor = std::max(cursor, state.tick);
                    measureEnd = std::max(measureEnd, state.tick);
                }
                else xml.skipCurrentElement();
            }
        } else if (xml.name() == u"backup") {
            while (xml.readNextStartElement()) {
                if (xml.name() == u"duration") {
                    const qint64 value = xml.readElementText().toLongLong();
                    auto& state = voiceCursors[activeVoiceKey];
                    state.tick = std::max(measureStart, state.tick - std::max<qint64>(1, value * music::MusicDocument::kPpq / std::max(1, divisions)));
                    cursor = state.tick;
                }
                else xml.skipCurrentElement();
            }
        } else if (xml.name() == u"direction") {
            parseDirection(xml, document, track, measureInfo, cursor);
        } else if (xml.name() == u"barline") {
            parseBarline(xml, measureInfo);
        } else {
            xml.skipCurrentElement();
        }
    }
    document.setDuration(std::max(document.duration(), measureEnd));
    measureInfo.duration = measureEnd - measureStart;
    track.measures.push_back(measureInfo);
    return measureEnd;
}

} // namespace

ReadResult MusicXmlReader::read(const QString& path) const
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return {nullptr, QStringLiteral("无法打开 MusicXML: %1").arg(file.errorString())};
    }
    const QByteArray sourceBytes = file.readAll();
    QString transformError;
    QByteArray normalizedBytes = sourceBytes;
    QXmlStreamReader rootProbe(sourceBytes);
    while (rootProbe.readNextStartElement()) {
        if (rootProbe.name() == u"score-timewise") {
            normalizedBytes = convertTimewiseToPartwise(sourceBytes, &transformError);
        }
        break;
    }
    if (normalizedBytes.isEmpty()) return {nullptr, transformError};
    QBuffer buffer;
    buffer.setData(normalizedBytes);
    if (!buffer.open(QIODevice::ReadOnly)) {
        return {nullptr, QStringLiteral("无法读取 MusicXML 数据")};
    }
    QXmlStreamReader xml(&buffer);
    auto document = std::make_shared<music::MusicDocument>();
    QHash<QString, InstrumentInfo> instruments;
    QHash<QString, InstrumentInfo> partInstruments;
    QHash<QString, QString> partNames;
    while (xml.readNextStartElement()) {
        if (xml.name() != u"score-partwise") {
            return {nullptr, QStringLiteral("不是 score-partwise MusicXML")};
        }
        while (xml.readNextStartElement()) {
            if (xml.name() == u"work") {
                while (xml.readNextStartElement()) {
                    if (xml.name() == u"work-title") document->setTitle(xml.readElementText());
                    else xml.skipCurrentElement();
                }
            } else if (xml.name() == u"part-list") {
                while (xml.readNextStartElement()) {
                    if (xml.name() == u"score-part") parseScorePart(xml, instruments, partNames, partInstruments);
                    else xml.skipCurrentElement();
                }
            } else if (xml.name() == u"part") {
                const QString id = xml.attributes().value(u"id").toString();
                music::Track track;
                track.id = id;
                track.name = partNames.value(id, id);
                track.program = partInstruments.value(id).program;
                track.channel = partInstruments.value(id).channel;
                track.percussion = partInstruments.value(id).percussion;
                if (track.percussion) track.channel = 9;
                int divisions = 1;
                music::Tick measureStart = 0;
                int measureNumber = 0;
                 QString currentInstrument;
                 QHash<QString, qint64> durationRemainders;
                while (xml.readNextStartElement()) {
                    if (xml.name() == u"measure") {
                        bool numericNumber = false;
                        const int parsedNumber = xml.attributes().value(u"number").toInt(&numericNumber);
                        if (numericNumber) measureNumber = parsedNumber;
                        else ++measureNumber;
                        measureStart = parseMeasure(xml, track, *document, measureStart, divisions, measureNumber,
                                                    instruments, currentInstrument, durationRemainders);
                    } else xml.skipCurrentElement();
                }
                for (auto& hairpin : track.hairpins) {
                    if (hairpin.end <= hairpin.start) hairpin.end = document->duration();
                }
                if (!track.notes.isEmpty()) document->tracks().push_back(std::move(track));
            } else xml.skipCurrentElement();
        }
    }
    if (xml.hasError()) return {nullptr, QStringLiteral("MusicXML 解析错误: %1").arg(xml.errorString())};
    if (document->tempos().isEmpty()) document->tempos().push_back({0, 120.0});
    std::sort(document->tempos().begin(), document->tempos().end(), [](const auto& a, const auto& b) { return a.tick < b.tick; });
    quint64 nextNoteId = 1;
    for (auto& track : document->tracks()) {
        for (auto& note : track.notes) note.noteId = nextNoteId++;
        for (const auto& key : track.keySignatures) document->keySignatures().push_back(key);
        for (const auto& lyric : track.lyrics) document->lyrics().push_back(lyric);
    }
    document->rebuildMeasureGrid();
    document->rebuildTempoMap();
    music::MusicAnalyzer().analyze(*document);
    if (!document->isValid()) return {nullptr, QStringLiteral("MusicXML 未包含可播放音符")};
    return {std::move(document), {}};
}

} // namespace midi_play::musicxml
