#include "musicxmlreader.h"
// 实现 MusicXML 结构归一化、流式语义提取与领域文档定稿。
#include "domain/music/musicanalysis.h"

#include <QFile>
#include <QFileInfo>
#include <QBuffer>
#include <QDomDocument>
#include <QXmlStreamReader>
#include <QMap>

#include <algorithm>
#include <cmath>
#include <numeric>

namespace midi_play::musicxml {
namespace {

// 将按小节组织的 timewise 转为按声部组织的 partwise，复用同一套小节解析逻辑。
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

    // 从 part-list 建立目标声部；标题等非小节节点及根属性原样复制。
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

    // 每个源小节分发到所有声明声部；缺失声部内容时仍保留带属性的空小节。
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

// part-list 中的乐器信息，program/channel 已转换为从零开始的 MIDI 编号。
struct InstrumentInfo {
    QString name;
    int program = 0;
    int channel = 0;
    bool percussion = false;
};

// 将自然音名映射为十二平均律音级，未知音名按 C 回退。
int stepToPitch(const QString& step)
{
    static const QHash<QString, int> values {{QStringLiteral("C"), 0}, {QStringLiteral("D"), 2},
                                             {QStringLiteral("E"), 4}, {QStringLiteral("F"), 5},
                                             {QStringLiteral("G"), 7}, {QStringLiteral("A"), 9},
                                             {QStringLiteral("B"), 11}};
    return values.value(step.toUpper(), 0);
}

// 保存记谱音名的七级序号，供简谱和记谱音高分析使用。
int stepToIndex(const QString& step)
{
    static const QHash<QString, int> values {{QStringLiteral("C"), 0}, {QStringLiteral("D"), 1},
                                             {QStringLiteral("E"), 2}, {QStringLiteral("F"), 3},
                                             {QStringLiteral("G"), 4}, {QStringLiteral("A"), 5},
                                             {QStringLiteral("B"), 6}};
    return values.value(step.toUpper(), 0);
}

// 读取 midi-instrument 子树中的音色，将 MusicXML 的 1..128 转为 MIDI 的 0..127。
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

// 读取 score-instrument 声明；名称中的 drum/percussion 用作打击乐识别线索。
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

// 合并声部名、乐器声明和 MIDI 参数，为后续 part 解析建立默认乐器映射。
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
    // 默认映射采用本轮遍历保存的乐器 ID；音符上的 instrument 可随后引入音色变化。
    if (!firstInstrumentId.isEmpty()) partInstruments.insert(id, instruments.value(firstInstrumentId));
}

// 读取音高子树，按八度、自然音名和升降号合成并限制到 MIDI 音高范围。
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

// 读取每四分音符的分割数，使用至少一单位防止后续时值换算除零。
int parseDivisions(QXmlStreamReader& xml)
{
    int divisions = 1;
    while (xml.readNextStartElement()) {
        if (xml.name() == u"divisions") divisions = std::max(1, xml.readElementText().toInt());
        else xml.skipCurrentElement();
    }
    return divisions;
}

// 将力度记号近似映射到 MIDI velocity，未识别记号使用默认力度 90。
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

// 读取速度、力度、文字、踏板、渐变和跳转标记；反复展开由音乐领域完成。
void parseDirection(QXmlStreamReader& xml, music::MusicDocument& document, music::Track& track,
                    music::Measure& measure, music::Tick tick, int divisions)
{
    double bpm = -1;
    double soundBpm = -1;
    double beatUnit = 0;
    music::Tick offset = 0;
    int velocity = -1;
    while (xml.readNextStartElement()) {
        // sound 提供实际播放参数；metronome 则提供记谱速度和节拍单位。
        if (xml.name() == u"sound") {
            const auto value = xml.attributes().value(u"tempo");
            if (!value.isEmpty()) soundBpm = value.toDouble();
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
                    double perMinute = -1;
                    double unit = 0;
                    int dots = 0;
                    int unitCount = 0;
                    while (xml.readNextStartElement()) {
                        if (xml.name() == u"per-minute") perMinute = xml.readElementText().toDouble();
                        else if (xml.name() == u"beat-unit") {
                            ++unitCount;
                            const QString name = xml.readElementText();
                            static const QHash<QString, double> lengths {
                                {QStringLiteral("maxima"), 32}, {QStringLiteral("long"), 16},
                                {QStringLiteral("breve"), 8}, {QStringLiteral("whole"), 4},
                                {QStringLiteral("half"), 2}, {QStringLiteral("quarter"), 1},
                                {QStringLiteral("eighth"), 0.5}, {QStringLiteral("16th"), 0.25},
                                {QStringLiteral("32nd"), 0.125}, {QStringLiteral("64th"), 0.0625},
                                {QStringLiteral("128th"), 0.03125}, {QStringLiteral("256th"), 0.015625},
                                {QStringLiteral("512th"), 0.0078125}, {QStringLiteral("1024th"), 0.00390625}
                            };
                            unit = lengths.value(name, 0);
                        } else if (xml.name() == u"beat-unit-dot") {
                            dots = std::min(dots + 1, 8);
                            xml.skipCurrentElement();
                        }
                        else xml.skipCurrentElement();
                    }
                    // 多个 beat-unit 表示节拍等式，不能作为每分钟速度直接解释。
                    if (unitCount == 1 && unit > 0 && std::isfinite(perMinute) && perMinute > 0) {
                        // 附点几何级数给出实际拍长，再换算为每分钟四分音符数。
                        beatUnit = unit * (2.0 - std::ldexp(1.0, -dots));
                        bpm = perMinute * beatUnit;
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
                    // 踏板记号转成 CC64；当前实现将 change 视为踏板按下。
                    const auto type = xml.attributes().value(u"type");
                    const int value = (type == u"start" || type == u"change") ? 127 : 0;
                    track.controlChanges.push_back({tick, track.channel, 64, value});
                    xml.skipCurrentElement();
                } else if (xml.name() == u"wedge") {
                    const auto type = xml.attributes().value(u"type");
                    if (type == u"stop") {
                        // 渐强/渐弱结束匹配最近一个尚未闭合的区间。
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
        } else if (xml.name() == u"offset") {
            // 仅显式 sound="yes" 的 offset 参与本函数的速度位置换算。
            const bool affectsSound = xml.attributes().value(u"sound") == u"yes";
            bool ok = false;
            const double value = xml.readElementText().toDouble(&ok);
            const double ticks = value * music::MusicDocument::kPpq / divisions;
            if (affectsSound && ok && std::isfinite(ticks) && std::abs(ticks) < 1e12) {
                offset = static_cast<music::Tick>(std::llround(ticks));
            }
        } else xml.skipCurrentElement();
    }
    // sound@tempo 已是四分音符/分钟，无论元素顺序如何都优先于记谱速度。
    // offset 调整速度与节拍单位的位置；其他方向事件仍使用传入的 tick。
    if (std::isfinite(soundBpm) && soundBpm > 0) bpm = soundBpm;
    const music::Tick tempoTick = std::max<music::Tick>(0, tick + offset);
    if (std::isfinite(bpm) && bpm > 0) {
        const auto sequence = static_cast<quint64>(document.tempos().size());
        document.tempos().push_back({tempoTick, bpm, sequence});
    }
    if (beatUnit > 0) document.metronomeUnits().push_back({tempoTick, beatUnit});
    if (velocity > 0) track.dynamics.push_back({tick, velocity});
}

// 汇总延音连线和演奏法；使用逻辑或合并，避免多处记号互相覆盖。
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

// 保存反复起止、次数和房子标记，导入阶段不复制反复段中的音符。
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

// 单个 note 元素的暂存语义，尚未应用 divisions 换算或声部游标定位。
struct ParsedNote {
    QString step; // 记谱音名字母，尚未换算为 MIDI pitch。
    int alter = 0; // 升降半音数，保留等音拼写。
    int octave = 4; // MusicXML 八度编号，C4 对应 MIDI 60。
    int durationValue = 0; // 源 divisions 单位，不能直接当作领域 tick。
    int voice = 1; // 非正或缺失声部回退为 1，与 staff 共同构成游标键。
    int staff = 1; // 记谱谱表编号，不等于 MIDI 通道。
    bool rest = false;
    bool unpitched = false;
    bool chord = false; // 与当前声部前一已记录音符共享起点，不推进声部时钟。
    bool grace = false; // 当前实现忽略装饰音的播放事件和游标推进。
    int velocity = 90;
    bool tieStart = false;
    bool tieStop = false;
    bool staccato = false;
    bool accent = false;
    bool tenuto = false;
    bool marcato = false;
    bool tremolo = false;
    QString instrumentId; // part-list 中声明的乐器 ID，空值表示没有显式切换。
    QString lyric;
    int lyricVerse = 1;
};

// 解析音符、休止和装饰音标志；尚不决定这些元素是否进入播放轨道。
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
            // 保存歌词文本和至少为一的段号，供文档级歌词投影使用。
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

// 按谱表/声部分别推进小节游标；返回所有声部的最晚结束位置作为下一小节起点。
music::Tick parseMeasure(QXmlStreamReader& xml, music::Track& track, music::MusicDocument& document,
                         music::Tick measureStart, int& divisions, int measureNumber,
                         const QHash<QString, InstrumentInfo>& instruments, QString& currentInstrument,
                         QHash<QString, qint64>& durationRemainders)
{
    music::Tick cursor = measureStart;
    music::Tick measureEnd = measureStart;
    // tick 定位下一音符，previousStart 用于和弦共享起点，remainder 缓存时值余数。
    struct VoiceCursor { music::Tick tick = 0; music::Tick previousStart = 0; qint64 remainder = 0; };
    // 小节内游标独立，跨小节只保留各声部的 divisions 余数，避免声部之间传递舍入误差。
    QHash<QString, VoiceCursor> voiceCursors;
    QString activeVoiceKey = QStringLiteral("1/1");
    voiceCursors.insert(activeVoiceKey, {measureStart, measureStart, 0});
    music::Measure measureInfo;
    measureInfo.number = measureNumber;
    measureInfo.start = measureStart;
    measureInfo.implicit = xml.attributes().value(u"implicit") == u"yes";
    while (xml.readNextStartElement()) {
        if (xml.name() == u"attributes") {
            while (xml.readNextStartElement()) {
                if (xml.name() == u"divisions") {
                    const int nextDivisions = std::max(1, xml.readElementText().toInt());
                    // 余数依赖 divisions；分辨率变更时不能沿用旧单位下的余数。
                    if (nextDivisions != divisions) durationRemainders.clear();
                    divisions = nextDivisions;
                }
                else if (xml.name() == u"time") {
                    // 加法拍号及不同分母的组合拍号统一到公分母，并保留分组用于重音。
                    QVector<int> groups;
                    QVector<int> pending;
                    int beatType = 1;
                    bool valid = true;
                    while (xml.readNextStartElement()) {
                        if (xml.name() == u"beats") {
                            pending.clear();
                            const auto values = xml.readElementText().split(u'+');
                            for (const auto& value : values) {
                                bool ok = false;
                                const int group = value.trimmed().toInt(&ok);
                                valid &= ok && group > 0 && group <= 1024;
                                if (pending.size() >= 1024) valid = false;
                                else pending.push_back(valid ? group : 1);
                            }
                        } else if (xml.name() == u"beat-type") {
                            bool ok = false;
                            const int denominator = xml.readElementText().toInt(&ok);
                            if (!ok || denominator < 1 || denominator > 1024 || pending.isEmpty()) {
                                valid = false;
                                continue;
                            }
                            // 限制公分母与分组数量，防止异常拍号导致整数或网格膨胀。
                            const int common = std::lcm(beatType, denominator);
                            if (common > 1024) { valid = false; continue; }
                            // 公分母提高时同步缩放已有分组，保持各分组的实际音乐长度。
                            for (auto& group : groups) group *= common / beatType;
                            for (int group : pending) {
                                if (groups.size() >= 1024) valid = false;
                                else groups.push_back(group * (common / denominator));
                            }
                            beatType = common;
                            pending.clear();
                        }
                        else xml.skipCurrentElement();
                    }
                    const qint64 beats = std::accumulate(groups.cbegin(), groups.cend(), qint64(0));
                    valid &= pending.isEmpty() && beats > 0 && beats <= 1024;
                    // 非法拍号以零分子交给领域层处理，不能静默制造看似有效的节拍。
                    music::TimeSignatureChange signature {cursor, valid ? static_cast<int>(beats) : 0, beatType};
                    if (valid && groups.size() > 1) signature.beatGroups = groups;
                    track.timeSignatures.push_back(signature);
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
            // 将除法余数跨音符/小节累计，减小连音时值逐音截断产生的时间漂移。
            const qint64 scaled = static_cast<qint64>(parsed.durationValue) * music::MusicDocument::kPpq + state.remainder;
            const music::Tick duration = std::max<music::Tick>(1, scaled / std::max(1, divisions));
            // 和弦与装饰音不消耗时钟余数，否则同时起音也会引入累计时间偏移。
            if (!parsed.chord && !parsed.grace) {
                state.remainder = scaled % std::max(1, divisions);
                durationRemainders.insert(activeVoiceKey, state.remainder);
            }
            // 和弦音复用上一音符起点；休止推进时间，装饰音不生成事件也不推进游标。
            const music::Tick noteStart = parsed.chord ? state.previousStart : state.tick;
            // 仅生成可演奏音符；休止仍推进时间，缺少音高的非打击乐元素不制造默认 C 音符。
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
                // 同时保存实际 MIDI 音高和记谱拼写，升降号含义不由音高反推。
                event.writtenPitch = {stepToIndex(parsed.step), parsed.alter, parsed.octave, event.pitch};
                event.hasWrittenPitch = true;
                track.notes.push_back(event);
                state.previousStart = noteStart;
            }
            if (!parsed.chord && !parsed.grace) state.tick += duration;
            cursor = std::max(cursor, state.tick);
            QString noteInstrument = parsed.instrumentId;
            // instrument 切换作为独立事件保存，由播放模型按时间应用。
            if (!noteInstrument.isEmpty() && noteInstrument != currentInstrument) {
                currentInstrument = noteInstrument;
                const auto info = instruments.value(noteInstrument);
                track.instrumentChanges.push_back({noteStart, info.channel, info.program, noteInstrument});
            }
            if (!parsed.lyric.isEmpty()) track.lyrics.push_back({noteStart, parsed.lyric,
                                                                  parsed.lyricVerse,
                                                                  static_cast<quint64>(track.lyrics.size())});
            // 以各声部推进后的最大值累积小节长度，backup 回退不会缩短已读入的内容。
            measureEnd = std::max(measureEnd, state.tick);
        } else if (xml.name() == u"forward") {
            // 当前实现对最近活动声部执行空白推进，并扩大本小节结束位置。
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
            // 当前活动声部回退但不能越过小节起点；已记录的小节最大长度不缩短。
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
            parseDirection(xml, document, track, measureInfo, cursor, divisions);
        } else if (xml.name() == u"barline") {
            parseBarline(xml, measureInfo);
        } else {
            xml.skipCurrentElement();
        }
    }
    document.setDuration(std::max(document.duration(), measureEnd));
    // 记录实际导入长度；弱起与缺拍信息由 implicit 和后续小节网格共同解释。
    measureInfo.duration = measureEnd - measureStart;
    track.measures.push_back(measureInfo);
    return measureEnd;
}

} // namespace

// 文件读取、结构转换、声部导入和领域分析都成功后才发布文档。
ReadResult MusicXmlReader::read(const QString& path) const
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return {nullptr, QStringLiteral("无法打开 MusicXML: %1").arg(file.errorString())};
    }
    const QByteArray sourceBytes = file.readAll();
    QString transformError;
    QByteArray normalizedBytes = sourceBytes;
    // 只探测根元素；timewise 经 DOM 重排后，仍走下方的流式 partwise 解析。
    QXmlStreamReader rootProbe(sourceBytes);
    while (rootProbe.readNextStartElement()) {
        if (rootProbe.name() == u"score-timewise") {
            normalizedBytes = convertTimewiseToPartwise(sourceBytes, &transformError);
        }
        break;
    }
    // 转换失败不继续解析原始 timewise，也不向调用方发布半构建文档。
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
                // 每个 part 从零起算；divisions、乐器和时值余数跨小节保留。
                int divisions = 1;
                music::Tick measureStart = 0;
                int measureNumber = 0;
                 QString currentInstrument;
                 QHash<QString, qint64> durationRemainders;
                while (xml.readNextStartElement()) {
                    if (xml.name() == u"measure") {
                        bool numericNumber = false;
                        const int parsedNumber = xml.attributes().value(u"number").toInt(&numericNumber);
                        // 非数字小节号使用递增序号，避免直接转换成零。
                        if (numericNumber) measureNumber = parsedNumber;
                        else ++measureNumber;
                        measureStart = parseMeasure(xml, track, *document, measureStart, divisions, measureNumber,
                                                    instruments, currentInstrument, durationRemainders);
                    } else xml.skipCurrentElement();
                }
                // 未闭合的渐变区间延长到当前已知文档末尾。
                for (auto& hairpin : track.hairpins) {
                    if (hairpin.end <= hairpin.start) hairpin.end = document->duration();
                }
                // 当前演奏列表只保留含音符的轨道，纯休止/纯元数据 part 不进入播放模型。
                if (!track.notes.isEmpty()) document->tracks().push_back(std::move(track));
            } else xml.skipCurrentElement();
        }
    }
    if (xml.hasError()) return {nullptr, QStringLiteral("MusicXML 解析错误: %1").arg(xml.errorString())};
    if (document->tempos().isEmpty()) document->tempos().push_back({0, 120.0});
    // 稳定排序保留同 tick 导入顺序，tempo map 随后以最后的有效速度覆盖该边界。
    std::stable_sort(document->tempos().begin(), document->tempos().end(), [](const auto& a, const auto& b) { return a.tick < b.tick; });
    // 为所有音符分配文档内唯一 ID，并汇总轨道调号和歌词。
    quint64 nextNoteId = 1;
    for (auto& track : document->tracks()) {
        for (auto& note : track.notes) note.noteId = nextNoteId++;
        for (const auto& key : track.keySignatures) document->keySignatures().push_back(key);
        for (const auto& lyric : track.lyrics) document->lyrics().push_back(lyric);
    }
    // 在向外共享文档前建立小节网格、速度时间索引和演奏派生语义。
    document->rebuildMeasureGrid();
    document->rebuildTempoMap();
    music::MusicAnalyzer().analyze(*document);
    if (!document->isValid()) return {nullptr, QStringLiteral("MusicXML 未包含可播放音符")};
    return {std::move(document), {}};
}

} // namespace midi_play::musicxml
