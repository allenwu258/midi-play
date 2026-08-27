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

void parseDirection(QXmlStreamReader& xml, music::MusicDocument& document, music::Tick tick)
{
    double bpm = -1;
    while (xml.readNextStartElement()) {
        if (xml.name() == u"sound") {
            const auto value = xml.attributes().value(u"tempo");
            if (!value.isEmpty()) bpm = value.toDouble();
            xml.skipCurrentElement();
        } else if (xml.name() == u"direction-type") {
            while (xml.readNextStartElement()) {
                if (xml.name() == u"metronome") {
                    while (xml.readNextStartElement()) {
                        if (xml.name() == u"per-minute") bpm = xml.readElementText().toDouble();
                        else xml.skipCurrentElement();
                    }
                } else xml.skipCurrentElement();
            }
        } else xml.skipCurrentElement();
    }
    if (bpm > 0.0) document.tempos().push_back({tick, bpm});
}

void parseNote(QXmlStreamReader& xml, music::Track& track, music::Tick& cursor,
               music::Tick& previousStart, int divisions, int defaultProgram, int defaultStaff)
{
    QString step;
    int alter = 0;
    int octave = 4;
    int durationValue = 0;
    int voice = 1;
    int staff = defaultStaff;
    bool isRest = false;
    bool isChord = false;
    bool isGrace = false;
    int velocity = 90;
    if (xml.attributes().hasAttribute(u"dynamics")) {
        velocity = std::clamp(xml.attributes().value(u"dynamics").toInt(), 1, 127);
    }
    while (xml.readNextStartElement()) {
        if (xml.name() == u"pitch") {
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
        else if (xml.name() == u"dynamics") velocity = std::clamp(xml.readElementText().toInt(), 1, 127);
        else xml.skipCurrentElement();
    }
    const music::Tick duration = std::max<music::Tick>(1, static_cast<music::Tick>(durationValue) * music::MusicDocument::kPpq / divisions);
    const music::Tick start = isChord ? previousStart : cursor;
    if (!isRest && !isGrace && !step.isEmpty()) {
        track.notes.push_back({start, duration, std::clamp((octave + 1) * 12 + stepToPitch(step) + alter, 0, 127), velocity,
                               track.channel, defaultProgram, voice, staff, false, isGrace});
        previousStart = start;
    }
    if (!isChord && !isGrace) cursor += duration;
    Q_UNUSED(voice)
}

music::Tick parseMeasure(QXmlStreamReader& xml, music::Track& track, music::MusicDocument& document,
                         music::Tick measureStart, int& divisions, int measureNumber)
{
    music::Tick cursor = measureStart;
    music::Tick measureEnd = measureStart;
    music::Tick previousStart = measureStart;
    while (xml.readNextStartElement()) {
        if (xml.name() == u"attributes") {
            while (xml.readNextStartElement()) {
                if (xml.name() == u"divisions") divisions = std::max(1, xml.readElementText().toInt());
                else xml.skipCurrentElement();
            }
        } else if (xml.name() == u"note") {
            parseNote(xml, track, cursor, previousStart, divisions, track.program, 1);
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
            parseDirection(xml, document, cursor);
        } else {
            xml.skipCurrentElement();
        }
    }
    document.setDuration(std::max(document.duration(), measureEnd));
    track.measures.push_back({measureNumber, measureStart, measureEnd - measureStart});
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
                int divisions = 1;
                music::Tick measureStart = 0;
                int measureNumber = 0;
                while (xml.readNextStartElement()) {
                    if (xml.name() == u"measure") {
                        bool numericNumber = false;
                        const int parsedNumber = xml.attributes().value(u"number").toInt(&numericNumber);
                        if (numericNumber) measureNumber = parsedNumber;
                        else ++measureNumber;
                        measureStart = parseMeasure(xml, track, *document, measureStart, divisions, measureNumber);
                    } else xml.skipCurrentElement();
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
