#include "fallingnotesrenderer.h"

#include <QFontMetricsF>
#include <QPainter>
#include <QPainterPath>

#include <algorithm>
#include <cmath>

namespace midi_play::presentation::visualization {
namespace {

using midi_play::visualization::ColorRgba;
using midi_play::visualization::PlaybackSceneState;
using midi_play::visualization::VisualChart;
using midi_play::visualization::VisualNote;

QColor toColor(const ColorRgba& color)
{
    return QColor(color.red, color.green, color.blue, color.alpha);
}

qreal yForTime(const PlaybackSceneGeometry& geometry, qint64 timeUs, qint64 positionUs)
{
    return geometry.strikeLineY
        - static_cast<qreal>(timeUs - positionUs) * geometry.pixelsPerMicrosecond;
}

QRectF horizontalRect(const PlaybackSceneGeometry& geometry, const VisualNote& note)
{
    qreal center = 0.0;
    qreal width = 0.0;
    if (note.isPercussion()) {
        const auto* slot = geometry.drumSlot(note.drumLane);
        if (!slot) return {};
        center = slot->centerX;
        width = slot->noteWidth;
    } else {
        const auto* slot = geometry.pitchSlot(note.pitch);
        if (!slot) return {};
        center = slot->centerX;
        width = slot->noteWidth;
    }
    if (note.coincidentCount > 1) {
        const qreal partWidth = width / note.coincidentCount;
        const qreal left = center - width * 0.5 + note.coincidentIndex * partWidth;
        return QRectF(left + 0.5, 0.0, std::max<qreal>(1.0, partWidth - 1.0), 0.0);
    }
    return QRectF(center - width * 0.5, 0.0, width, 0.0);
}

bool isActive(const VisualNote& note, qint64 positionUs)
{
    return note.startUs <= positionUs && note.audibleEndUs > positionUs;
}

QColor noteColor(const VisualChart& chart, const VisualNote& note)
{
    QColor color = toColor(chart.tracks().at(note.trackIndex).color);
    const qreal velocity = std::clamp(note.velocity / 127.0, 0.0, 1.0);
    color = color.lighter(static_cast<int>(88.0 + velocity * 30.0));
    color.setAlpha(note.isGhost() ? 120 : static_cast<int>(190 + velocity * 55));
    return color;
}

} // namespace

void FallingNotesRenderer::render(QPainter& painter, const PlaybackSceneGeometry& geometry,
                                  const PlaybackSceneState& state) const
{
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.fillRect(geometry.bounds, m_theme.background);
    drawGrid(painter, geometry, state);
    drawNotes(painter, geometry, state);
    drawStrikeLine(painter, geometry, state);
    drawKeyboard(painter, geometry, state);
    drawOverlay(painter, geometry, state);
    painter.restore();
}

void FallingNotesRenderer::drawGrid(QPainter& painter, const PlaybackSceneGeometry& geometry,
                                    const PlaybackSceneState& state) const
{
    painter.save();
    painter.setClipRect(geometry.fallingRect);
    const qreal gridLeft = geometry.pianoRect.left();
    const qreal gridRight = geometry.drumRect.isEmpty() ? geometry.pianoRect.right() : geometry.drumRect.right();

    // Subtle pitch bands preserve keyboard alignment without recreating a spreadsheet.
    for (const auto& slot : geometry.pitches) {
        if (!slot.valid || !slot.blackKey) continue;
        const QRectF band(slot.keyRect.left(), geometry.fallingRect.top(),
                          slot.keyRect.width(), geometry.fallingRect.height());
        painter.fillRect(band, QColor(255, 255, 255, 7));
    }
    if (!state.chart) {
        painter.restore();
        return;
    }

    const qint64 windowStart = state.transportPositionUs - state.afterglowUs;
    const qint64 windowEnd = state.transportPositionUs + state.lookAheadUs;
    const auto& lines = state.chart->gridLines();
    auto it = std::lower_bound(lines.cbegin(), lines.cend(), windowStart,
                               [](const auto& line, qint64 value) { return line.timeUs < value; });
    QFont labelFont = painter.font();
    labelFont.setPointSizeF(8.0);
    painter.setFont(labelFont);
    for (; it != lines.cend() && it->timeUs <= windowEnd; ++it) {
        const qreal y = yForTime(geometry, it->timeUs, state.transportPositionUs);
        QPen pen(it->measureStart ? m_theme.measureLine : m_theme.beatLine);
        pen.setWidthF(it->measureStart ? 1.25 : 1.0);
        painter.setPen(pen);
        painter.drawLine(QPointF(gridLeft, y), QPointF(gridRight, y));
        if (it->measureStart) {
            painter.setPen(m_theme.subtleText);
            painter.drawText(QRectF(4.0, y - 10.0, gridLeft - 9.0, 20.0),
                             Qt::AlignRight | Qt::AlignVCenter,
                             QStringLiteral("M%1").arg(it->measureNumber));
        }
    }
    painter.restore();
}

void FallingNotesRenderer::drawNotes(QPainter& painter, const PlaybackSceneGeometry& geometry,
                                     const PlaybackSceneState& state) const
{
    if (!state.chart) return;
    painter.save();
    painter.setClipRect(geometry.fallingRect);

    for (const int noteIndex : state.visibleNoteIndices) {
        if (noteIndex < 0 || noteIndex >= state.chart->notes().size()) continue;
        const auto& note = state.chart->notes()[noteIndex];
        QRectF xRect = horizontalRect(geometry, note);
        if (xRect.width() <= 0.0) continue;

        const qreal startY = yForTime(geometry, note.startUs, state.transportPositionUs);
        const qreal keyEndY = yForTime(geometry, note.keyEndUs, state.transportPositionUs);
        const qreal audibleEndY = yForTime(geometry, note.audibleEndUs, state.transportPositionUs);
        const qreal primaryTop = std::min(keyEndY, startY - 4.0);
        const qreal primaryBottom = startY;
        QRectF primary(xRect.left(), primaryTop, xRect.width(),
                       std::max<qreal>(4.0, primaryBottom - primaryTop));
        const QColor color = noteColor(*state.chart, note);

        if (note.audibleEndUs > note.keyEndUs) {
            QRectF tail(xRect.left() + xRect.width() * 0.2, audibleEndY,
                        xRect.width() * 0.6, std::max<qreal>(2.0, keyEndY - audibleEndY));
            QColor tailColor = color;
            tailColor.setAlpha(std::max(38, color.alpha() / 3));
            painter.setPen(Qt::NoPen);
            painter.setBrush(tailColor);
            painter.drawRect(tail);
        }

        QColor border = color.lighter(isActive(note, state.transportPositionUs) ? 145 : 112);
        border.setAlpha(240);
        QPen pen(border, isActive(note, state.transportPositionUs) ? 1.8 : 1.0);
        if (note.isGhost()) pen.setStyle(Qt::DashLine);
        painter.setPen(pen);
        painter.setBrush(color);
        painter.drawRect(primary);

        painter.setPen(QPen(border, 1.3));
        painter.drawLine(QPointF(primary.left() + 1.0, startY),
                         QPointF(primary.right() - 1.0, startY));

        if ((note.flags & midi_play::visualization::TremoloNote) != 0 && primary.height() > 14.0) {
            painter.setPen(QPen(QColor(255, 255, 255, 110), 1.0));
            const qreal centerY = std::clamp(primary.center().y(), primary.top() + 5.0, primary.bottom() - 5.0);
            painter.drawLine(QPointF(primary.left() + 3.0, centerY + 3.0),
                             QPointF(primary.right() - 3.0, centerY - 3.0));
        }

    }
    painter.restore();
}

void FallingNotesRenderer::drawStrikeLine(QPainter& painter, const PlaybackSceneGeometry& geometry,
                                          const PlaybackSceneState& state) const
{
    const qreal left = geometry.pianoRect.left();
    const qreal right = geometry.drumRect.isEmpty() ? geometry.pianoRect.right() : geometry.drumRect.right();
    QColor glow = m_theme.strikeLine;
    glow.setAlpha(45);
    painter.fillRect(QRectF(left, geometry.strikeLineY - 5.0, right - left, 10.0), glow);
    painter.setPen(QPen(m_theme.strikeLine, 2.0));
    painter.drawLine(QPointF(left, geometry.strikeLineY), QPointF(right, geometry.strikeLineY));

    if (!state.chart) return;
    const auto& labelNoteIndices = state.activeNoteLookup.melodicLabelNoteIndices();
    if (labelNoteIndices.isEmpty()) return;
    QFont font = painter.font();
    font.setPointSizeF(10.0);
    font.setWeight(QFont::DemiBold);
    painter.setFont(font);
    painter.setPen(m_theme.primaryText);
    const QFontMetricsF metrics(font);
    qreal x = left + 8.0;
    for (const int noteIndex : labelNoteIndices) {
        const auto& note = state.chart->notes().at(noteIndex);
        const qreal textWidth = metrics.horizontalAdvance(note.simplifiedLabel);
        if (x + textWidth > right - 8.0) break;
        const QRectF textRect(x, geometry.strikeLineY + 5.0, textWidth + 1.0, 19.0);
        painter.drawText(textRect, Qt::AlignCenter, note.simplifiedLabel);
        const int dotCount = std::min(3, std::abs(note.octaveOffset));
        if (dotCount > 0) {
            painter.setBrush(m_theme.primaryText);
            painter.setPen(Qt::NoPen);
            const qreal totalWidth = dotCount * 3.5 - 1.5;
            const qreal dotsLeft = textRect.center().x() - totalWidth * 0.5;
            const qreal dotY = note.octaveOffset > 0 ? textRect.top() - 1.5 : textRect.bottom() + 0.5;
            for (int dot = 0; dot < dotCount; ++dot) {
                painter.drawEllipse(QRectF(dotsLeft + dot * 3.5, dotY, 2.0, 2.0));
            }
            painter.setPen(m_theme.primaryText);
        }
        x += textWidth + 13.0;
    }
}

void FallingNotesRenderer::drawKeyboard(QPainter& painter, const PlaybackSceneGeometry& geometry,
                                        const PlaybackSceneState& state) const
{
    painter.fillRect(geometry.keyboardRect, m_theme.keyboardBackground);

    auto activeColorForNoteIndex = [&](int noteIndex) -> QColor {
        if (!state.chart) return {};
        return noteIndex >= 0 && noteIndex < state.chart->notes().size()
            ? noteColor(*state.chart, state.chart->notes().at(noteIndex))
            : QColor {};
    };

    // White keys establish the base; black keys are painted afterwards.
    for (const auto& slot : geometry.pitches) {
        if (!slot.valid || slot.blackKey) continue;
        QColor fill = activeColorForNoteIndex(state.activeNoteLookup.noteIndexForPitch(slot.pitch));
        if (!fill.isValid()) fill = m_theme.whiteKey;
        painter.setPen(QPen(m_theme.whiteKeyBorder, 0.8));
        painter.setBrush(fill);
        painter.drawRect(slot.keyRect.adjusted(0.0, 0.0, -0.5, -0.5));
    }
    for (const auto& slot : geometry.pitches) {
        if (!slot.valid || !slot.blackKey) continue;
        QColor fill = activeColorForNoteIndex(state.activeNoteLookup.noteIndexForPitch(slot.pitch));
        if (!fill.isValid()) fill = m_theme.blackKey;
        painter.setPen(QPen(m_theme.blackKeyBorder, 1.0));
        painter.setBrush(fill);
        painter.drawRect(slot.keyRect.adjusted(0.5, 0.0, -0.5, -1.0));
    }

    QFont keyFont = painter.font();
    keyFont.setPointSizeF(7.5);
    painter.setFont(keyFont);
    painter.setPen(QColor(44, 47, 46));
    for (const auto& slot : geometry.pitches) {
        if (!slot.valid || slot.blackKey || slot.pitch % 12 != 0 || slot.keyRect.width() < 12.0) continue;
        painter.drawText(slot.keyRect.adjusted(1.0, 0.0, -1.0, -5.0), Qt::AlignHCenter | Qt::AlignBottom,
                         QStringLiteral("C%1").arg(slot.pitch / 12 - 1));
    }

    if (state.chart && !geometry.drumRect.isEmpty()) {
        for (const auto& slot : geometry.drumSlots) {
            QColor fill = activeColorForNoteIndex(state.activeNoteLookup.noteIndexForDrumLane(slot.lane));
            if (!fill.isValid()) fill = QColor("#292d30");
            painter.setPen(QPen(QColor(255, 255, 255, 35), 0.8));
            painter.setBrush(fill);
            painter.drawRect(slot.keyRect.adjusted(0.0, 0.0, -0.5, -0.5));
            if (slot.lane < state.chart->drumLanes().size() && slot.keyRect.width() >= 18.0) {
                painter.setPen(m_theme.primaryText);
                const QString text = QFontMetricsF(keyFont).elidedText(
                    state.chart->drumLanes()[slot.lane].name, Qt::ElideRight, slot.keyRect.width() - 5.0);
                painter.drawText(slot.keyRect.adjusted(3.0, 4.0, -3.0, -4.0),
                                 Qt::AlignHCenter | Qt::AlignBottom, text);
            }
        }
    }
}

void FallingNotesRenderer::drawOverlay(QPainter& painter, const PlaybackSceneGeometry& geometry,
                                       const PlaybackSceneState& state) const
{
    if (state.chart) {
        const QString marker = state.chart->markerAt(state.transportPositionUs);
        const QString lyric = state.chart->lyricAt(state.transportPositionUs);
        QFont overlayFont = painter.font();
        overlayFont.setPointSizeF(9.5);
        painter.setFont(overlayFont);
        if (!marker.isEmpty()) {
            painter.setPen(m_theme.secondaryText);
            painter.drawText(QRectF(geometry.pianoRect.left(), geometry.fallingRect.top() + 8.0,
                                    geometry.pianoRect.width(), 24.0), Qt::AlignHCenter | Qt::AlignVCenter,
                             marker);
        }
        if (!lyric.isEmpty()) {
            painter.setPen(m_theme.primaryText);
            painter.drawText(QRectF(geometry.pianoRect.left(), geometry.strikeLineY - 34.0,
                                    geometry.pianoRect.width(), 24.0), Qt::AlignHCenter | Qt::AlignVCenter,
                             QFontMetricsF(overlayFont).elidedText(lyric, Qt::ElideRight,
                                                                  geometry.pianoRect.width() * 0.8));
        }
    }

    if (!state.loading && state.errorMessage.isEmpty() && state.chart) return;
    QColor veil(10, 11, 12, state.loading ? 118 : 148);
    painter.fillRect(geometry.fallingRect, veil);
    QFont statusFont = painter.font();
    statusFont.setPointSizeF(11.0);
    statusFont.setWeight(QFont::DemiBold);
    painter.setFont(statusFont);
    painter.setPen(state.errorMessage.isEmpty() ? m_theme.primaryText : m_theme.error);
    QString text;
    if (!state.errorMessage.isEmpty()) text = state.errorMessage;
    else if (state.loading) text = QStringLiteral("正在分析音乐文件...");
    else text = QStringLiteral("打开 MusicXML 或 MIDI 文件开始播放");
    painter.drawText(geometry.fallingRect.adjusted(24.0, 24.0, -24.0, -24.0),
                     Qt::AlignCenter | Qt::TextWordWrap, text);
}

} // namespace midi_play::presentation::visualization
