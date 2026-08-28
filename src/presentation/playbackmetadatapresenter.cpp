#include "playbackmetadatapresenter.h"

#include <QStringList>
#include <QtMath>

#include <algorithm>

namespace midi_play::presentation {
namespace {

QString keyName(int fifths, const QString& mode)
{
    static const QStringList majorNames {
        QStringLiteral("Cb"), QStringLiteral("Gb"), QStringLiteral("Db"), QStringLiteral("Ab"),
        QStringLiteral("Eb"), QStringLiteral("Bb"), QStringLiteral("F"), QStringLiteral("C"),
        QStringLiteral("G"), QStringLiteral("D"), QStringLiteral("A"), QStringLiteral("E"),
        QStringLiteral("B"), QStringLiteral("F#"), QStringLiteral("C#")
    };
    static const QStringList minorNames {
        QStringLiteral("Ab"), QStringLiteral("Eb"), QStringLiteral("Bb"), QStringLiteral("F"),
        QStringLiteral("C"), QStringLiteral("G"), QStringLiteral("D"), QStringLiteral("A"),
        QStringLiteral("E"), QStringLiteral("B"), QStringLiteral("F#"), QStringLiteral("C#"),
        QStringLiteral("G#"), QStringLiteral("D#"), QStringLiteral("A#")
    };
    const bool minor = mode.compare(QStringLiteral("minor"), Qt::CaseInsensitive) == 0;
    const int index = std::clamp(fifths, -7, 7) + 7;
    return (minor ? minorNames.at(index) : majorNames.at(index))
        + (minor ? QStringLiteral(" min") : QStringLiteral(" maj"));
}

} // namespace

PlaybackMetadata PlaybackMetadataPresenter::at(const midi_play::visualization::VisualChart* chart,
                                                qint64 positionUs)
{
    if (!chart) {
        return {QStringLiteral("--"), QStringLiteral("--/--"), QStringLiteral("-- BPM")};
    }

    const qint64 resolvedPosition = std::clamp<qint64>(positionUs, 0, chart->durationUs());
    const auto key = chart->keyAt(resolvedPosition);
    const auto signature = chart->timeSignatureAt(resolvedPosition);
    return {
        keyName(key.fifths, key.mode),
        QStringLiteral("%1/%2").arg(signature.beats).arg(signature.beatType),
        QStringLiteral("%1 BPM").arg(qRound(chart->tempoAt(resolvedPosition)))
    };
}

} // namespace midi_play::presentation
