#include "musicxmlreader.h"

#include <QFile>
#include <QFileInfo>
#include <QXmlStreamReader>

#include <algorithm>

namespace midi_play::musicxml {
namespace {

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
                    bool& staccato, bool& accent, bool& tenuto)
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

void parseNote(QXmlStreamReader& xml, music::Track& track, music::Tick& cursor,
               music::Tick& previousStart, int divisions, int defaultProgram, int defaultStaff,
               QString* instrumentId)
{
    QString step;
    int alter = 0;
    int octave = 4;
    int durationValue = 0;
    int voice = 1;
    int staff = defaultStaff;
    bool isRest = false;
    bool isUnpitched = false;
    bool isChord = false;
    bool isGrace = false;
    int velocity = 90;
    bool tieStart = false;
    bool tieStop = false;
    bool staccato = false;
    bool accent = false;
    bool tenuto = false;
    QString noteInstrumentId;
    if (xml.attributes().hasAttribute(u"dynamics")) {
        velocity = std::clamp(xml.attributes().value(u"dynamics").toInt(), 1, 127);
    }
    while (xml.readNextStartElement()) {
        if (xml.name() == u"pitch" || xml.name() == u"unpitched") {
            isUnpitched = xml.name() == u"unpitched";
            while (xml.readNextStartElement()) {
                if (xml.name() == u"step") step = xml.readElementText();
                else if (xml.name() == u"alter") alter = xml.readElementText().toInt();
                else if (xml.name() == u"octave") octave = xml.readElementText().toInt();
                else xml.skipCurrentElement();
            }
        } else if (xml.name() == u"rest") { isRest = true; xml.skipCurrentElement(); }
        else if (xml.name() == u"chord") { isChord = true; xml.skipCurrentElement(); }
        else if (xml.name() == u"grace") { isGrace = true; xml.skipCurrentElement(); }
        else if (xml.name() == u"duration") durationValue = xml.readElementText().toInt();
        else if (xml.name() == u"voice") voice = xml.readElementText().toInt();
        else if (xml.name() == u"staff") staff = std::max(1, xml.readElementText().toInt());
        else if (xml.name() == u"instrument") noteInstrumentId = xml.attributes().value(u"id").toString(), xml.skipCurrentElement();
        else if (xml.name() == u"dynamics") velocity = std::clamp(xml.readElementText().toInt(), 1, 127);
        else if (xml.name() == u"tie") {
            const auto type = xml.attributes().value(u"type");
            tieStart = tieStart || type == u"start";
            tieStop = tieStop || type == u"stop";
            xml.skipCurrentElement();
        } else if (xml.name() == u"notations") {
            parseNotations(xml, tieStart, tieStop, staccato, accent, tenuto);
        }
        else xml.skipCurrentElement();
    }
    const music::Tick duration = std::max<music::Tick>(1, static_cast<music::Tick>(durationValue) * music::MusicDocument::kPpq / divisions);
    const music::Tick start = isChord ? previousStart : cursor;
    if (!isRest && !isGrace && (!step.isEmpty() || isUnpitched)) {
        track.notes.push_back({start, duration, std::clamp((octave + 1) * 12 + stepToPitch(step) + alter, 0, 127), velocity,
                               track.channel, defaultProgram, voice, staff, false, isGrace, tieStart, tieStop,
                               staccato, accent, tenuto, false});
        previousStart = start;
    }
    if (!isChord && !isGrace) cursor += duration;
    if (instrumentId && !noteInstrumentId.isEmpty()) *instrumentId = noteInstrumentId;
    Q_UNUSED(voice)
}

music::Tick parseMeasure(QXmlStreamReader& xml, music::Track& track, music::MusicDocument& document,
                         music::Tick measureStart, int& divisions, int measureNumber,
                         const QHash<QString, InstrumentInfo>& instruments, QString& currentInstrument)
{
    music::Tick cursor = measureStart;
    music::Tick measureEnd = measureStart;
    music::Measure measureInfo;
    measureInfo.number = measureNumber;
    measureInfo.start = measureStart;
    music::Tick previousStart = measureStart;
    while (xml.readNextStartElement()) {
        if (xml.name() == u"attributes") {
            while (xml.readNextStartElement()) {
                if (xml.name() == u"divisions") divisions = std::max(1, xml.readElementText().toInt());
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
            QString noteInstrument;
            parseNote(xml, track, cursor, previousStart, divisions, track.program, 1, &noteInstrument);
            if (!noteInstrument.isEmpty() && noteInstrument != currentInstrument) {
                currentInstrument = noteInstrument;
                const auto info = instruments.value(noteInstrument);
                track.instrumentChanges.push_back({cursor, info.channel, info.program, noteInstrument});
            }
            measureEnd = std::max(measureEnd, cursor);
        } else if (xml.name() == u"forward") {
            while (xml.readNextStartElement()) {
                if (xml.name() == u"duration") {
                    cursor += static_cast<music::Tick>(xml.readElementText().toInt()) * music::MusicDocument::kPpq / divisions;
                    measureEnd = std::max(measureEnd, cursor);
                }
                else xml.skipCurrentElement();
            }
        } else if (xml.name() == u"backup") {
            while (xml.readNextStartElement()) {
                if (xml.name() == u"duration") cursor = std::max(measureStart, cursor - static_cast<music::Tick>(xml.readElementText().toInt()) * music::MusicDocument::kPpq / divisions);
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
    QXmlStreamReader xml(&file);
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
                while (xml.readNextStartElement()) {
                    if (xml.name() == u"measure") {
                        bool numericNumber = false;
                        const int parsedNumber = xml.attributes().value(u"number").toInt(&numericNumber);
                        if (numericNumber) measureNumber = parsedNumber;
                        else ++measureNumber;
                        measureStart = parseMeasure(xml, track, *document, measureStart, divisions, measureNumber,
                                                    instruments, currentInstrument);
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
    if (!document->isValid()) return {nullptr, QStringLiteral("MusicXML 未包含可播放音符")};
    return {std::move(document), {}};
}

} // namespace midi_play::musicxml
