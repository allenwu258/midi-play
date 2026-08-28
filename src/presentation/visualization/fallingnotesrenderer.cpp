#include "fallingnotesrenderer.h"
#include "rasterrenderpolicy.h"

#include <QPainter>

#include <algorithm>
#include <array>
#include <cmath>

namespace midi_play::presentation::visualization {
namespace {

using midi_play::visualization::PlaybackSceneState;
using midi_play::visualization::VisualNote;

qreal yForTime(const PlaybackSceneGeometry& geometry, qint64 timeUs, qint64 positionUs)
{
    return geometry.strikeLineY
        - static_cast<qreal>(timeUs - positionUs) * geometry.pixelsPerMicrosecond;
}

bool isActive(const VisualNote& note, qint64 positionUs)
{
    return note.startUs <= positionUs && note.audibleEndUs > positionUs;
}

qreal painterDevicePixelRatio(const QPainter& painter)
{
    return painter.device() ? painter.device()->devicePixelRatioF() : 1.0;
}

const QString& octaveLabelForPitch(int pitch)
{
    static const std::array<QString, 128> labels = [] {
        std::array<QString, 128> result;
        for (int value = 0; value < static_cast<int>(result.size()); ++value) {
            result[value] = QStringLiteral("C%1").arg(value / 12 - 1);
        }
        return result;
    }();
    static const QString empty;
    return pitch >= 0 && pitch < static_cast<int>(labels.size()) ? labels[pitch] : empty;
}

void drawPreparedText(QPainter& painter, const PreparedTextLayout& text,
                      const QRectF& rect, Qt::Alignment alignment)
{
    qreal x = rect.left();
    qreal y = rect.top();
    if (alignment.testFlag(Qt::AlignHCenter)) x += (rect.width() - text.size.width()) * 0.5;
    else if (alignment.testFlag(Qt::AlignRight)) x = rect.right() - text.size.width();
    if (alignment.testFlag(Qt::AlignVCenter)) y += (rect.height() - text.size.height()) * 0.5;
    else if (alignment.testFlag(Qt::AlignBottom)) y = rect.bottom() - text.size.height();
    painter.drawStaticText(QPointF(x, y), text.staticText);
}

} // namespace

void FallingNotesRenderer::render(QPainter& painter, const PlaybackSceneGeometry& geometry,
                                  const PlaybackSceneState& state)
{
    renderStaticLayer(painter, geometry, state);
    renderDynamicLayer(painter, geometry, state);
}

void FallingNotesRenderer::renderStaticLayer(QPainter& painter, const PlaybackSceneGeometry& geometry,
                                             const PlaybackSceneState& state)
{
    renderStaticBackgroundLayer(painter, geometry, state);
    renderStaticKeyboardLayer(painter, geometry, state);
}

void FallingNotesRenderer::renderStaticBackgroundLayer(
    QPainter& painter, const PlaybackSceneGeometry& geometry,
    const PlaybackSceneState& state)
{
    prepareScene(geometry, state);
    painter.save();
    RasterRenderPolicy::apply(painter);
    painter.fillRect(geometry.bounds, m_theme.background);
    drawPitchBands(painter, geometry);
    painter.restore();
}

void FallingNotesRenderer::renderStaticKeyboardLayer(
    QPainter& painter, const PlaybackSceneGeometry& geometry,
    const PlaybackSceneState& state)
{
    prepareScene(geometry, state);
    painter.save();
    RasterRenderPolicy::apply(painter);
    drawKeyboardBase(painter, geometry, state);
    painter.restore();
}

void FallingNotesRenderer::renderDynamicLayer(QPainter& painter, const PlaybackSceneGeometry& geometry,
                                              const PlaybackSceneState& state)
{
    prepareScene(geometry, state);
    m_activeNoteLookup.reset(state.chart ? state.chart->drumLanes().size() : 0);
    m_visibleNoteCount = 0;
    m_activeNoteCount = 0;
    painter.save();
    RasterRenderPolicy::apply(painter);
    drawTimeGrid(painter, geometry, state);
    drawNotes(painter, geometry, state);
    drawStrikeLine(painter, geometry, state);
    drawActiveKeyboard(painter, geometry, state);
    drawOverlay(painter, geometry, state);
    painter.restore();
}

void FallingNotesRenderer::prepareScene(const PlaybackSceneGeometry& geometry,
                                        const PlaybackSceneState& state)
{
    m_overlayTimeline.setChart(state.chart);
    m_noteRenderCache.prepare(state.chart, geometry,
                              {m_theme.keyboardBackground, m_theme.whiteKey});
    if (m_keyboardGeometryBuildCount != m_noteRenderCache.geometryBuildCount()) {
        m_keyboardGeometryBuildCount = m_noteRenderCache.geometryBuildCount();
        m_pitchBandRects.clear();
        m_blackKeyForegroundRects.clear();
        m_pitchBandRects.reserve(geometry.pitches.size());
        m_blackKeyForegroundRects.reserve(geometry.pitches.size());
        for (const auto& slot : geometry.pitches) {
            if (slot.valid && slot.blackKey) {
                m_pitchBandRects.push_back(QRectF(
                    slot.keyRect.left(), geometry.fallingRect.top(),
                    slot.keyRect.width(), geometry.fallingRect.height()));
                m_blackKeyForegroundRects.push_back(
                    slot.keyRect.adjusted(0.5, 0.0, -0.5, -1.0));
            }
        }
    }
    const qsizetype styleCount = m_noteRenderCache.styles().size();
    if (m_noteBatches.size() != styleCount) {
        m_noteBatches.resize(styleCount);
        m_batchEpochs.fill(0, styleCount);
        m_batchEpoch = 0;
        m_usedStyleIndices.clear();
    }
}

void FallingNotesRenderer::drawPitchBands(QPainter& painter,
                                          const PlaybackSceneGeometry& geometry) const
{
    if (m_pitchBandRects.isEmpty()) return;
    painter.save();
    painter.setClipRect(geometry.fallingRect);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(255, 255, 255, 7));
    painter.drawRects(m_pitchBandRects.constData(),
                      static_cast<int>(m_pitchBandRects.size()));
    painter.restore();
}

void FallingNotesRenderer::drawTimeGrid(QPainter& painter, const PlaybackSceneGeometry& geometry,
                                        const PlaybackSceneState& state)
{
    painter.save();
    painter.setClipRect(geometry.fallingRect);
    const qreal gridLeft = geometry.pianoRect.left();
    const qreal gridRight = geometry.drumRect.isEmpty() ? geometry.pianoRect.right() : geometry.drumRect.right();
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
        pen.setWidth(it->measureStart ? 2 : 1);
        painter.setPen(pen);
        painter.drawLine(QPointF(gridLeft, y), QPointF(gridRight, y));
        if (it->measureStart) {
            painter.setPen(m_theme.subtleText);
            const auto& text = m_textLayoutCache.layout(
                TextLayoutRole::Measure, it->measureLabel,
                labelFont, -1.0, painterDevicePixelRatio(painter));
            drawPreparedText(painter, text, QRectF(4.0, y - 10.0, gridLeft - 9.0, 20.0),
                             Qt::AlignRight | Qt::AlignVCenter);
        }
    }
    painter.restore();
}

void FallingNotesRenderer::drawNotes(QPainter& painter, const PlaybackSceneGeometry& geometry,
                                     const PlaybackSceneState& state)
{
    if (!state.chart) return;
    painter.save();
    painter.setClipRect(geometry.fallingRect);

    ++m_batchEpoch;
    if (m_batchEpoch == 0) {
        m_batchEpochs.fill(0);
        m_batchEpoch = 1;
    }
    m_usedStyleIndices.clear();
    m_tremoloLines.clear();

    for (const int noteIndex : state.candidateNoteIndices) {
        if (noteIndex < 0 || noteIndex >= state.chart->notes().size()) continue;
        const auto& note = state.chart->notes()[noteIndex];
        if (note.startUs > state.visibleWindowEndUs
            || note.audibleEndUs < state.visibleWindowStartUs) {
            continue;
        }
        ++m_visibleNoteCount;
        if (state.transportState == midi_play::playback::State::Playing
            && isActive(note, state.transportPositionUs)) {
            m_activeNoteLookup.add(noteIndex, note);
            ++m_activeNoteCount;
        }
        const auto* prepared = m_noteRenderCache.note(noteIndex);
        if (!prepared || !prepared->validGeometry || prepared->styleIndex < 0
            || prepared->styleIndex >= m_noteBatches.size()) {
            continue;
        }

        const int styleIndex = prepared->styleIndex;
        if (m_batchEpochs[styleIndex] != m_batchEpoch) {
            m_batchEpochs[styleIndex] = m_batchEpoch;
            m_noteBatches[styleIndex].clear();
            m_usedStyleIndices.push_back(styleIndex);
        }
        auto& batch = m_noteBatches[styleIndex];

        const qreal startY = yForTime(geometry, note.startUs, state.transportPositionUs);
        const qreal keyEndY = yForTime(geometry, note.keyEndUs, state.transportPositionUs);
        const qreal primaryTop = std::min(keyEndY, startY - 4.0);
        const QRectF primary(prepared->left, primaryTop, prepared->width,
                             std::max<qreal>(4.0, startY - primaryTop));

        if (prepared->hasTail) {
            const qreal audibleEndY = yForTime(
                geometry, note.audibleEndUs, state.transportPositionUs);
            batch.tails.push_back(QRectF(
                prepared->left + prepared->width * 0.2, audibleEndY,
                prepared->width * 0.6, std::max<qreal>(2.0, keyEndY - audibleEndY)));
        }

        const bool active = isActive(note, state.transportPositionUs);
        auto& bodies = active ? batch.activeBodies : batch.inactiveBodies;
        auto& attackLines = active ? batch.activeAttackLines : batch.inactiveAttackLines;
        bodies.push_back(primary);
        attackLines.push_back(QLineF(primary.left() + 1.0, startY,
                                     primary.right() - 1.0, startY));

        if (prepared->tremolo && primary.height() > 14.0) {
            const qreal centerY = std::clamp(primary.center().y(), primary.top() + 5.0, primary.bottom() - 5.0);
            m_tremoloLines.push_back(QLineF(primary.left() + 3.0, centerY + 3.0,
                                            primary.right() - 3.0, centerY - 3.0));
        }
    }

    const auto& styles = m_noteRenderCache.styles();
    painter.setPen(Qt::NoPen);
    for (const int styleIndex : m_usedStyleIndices) {
        const auto& batch = m_noteBatches[styleIndex];
        if (batch.tails.isEmpty()) continue;
        painter.setBrush(styles[styleIndex].tailBrush);
        painter.drawRects(batch.tails.constData(), static_cast<int>(batch.tails.size()));
    }

    for (const int styleIndex : m_usedStyleIndices) {
        const auto& style = styles[styleIndex];
        const auto& batch = m_noteBatches[styleIndex];
        painter.setBrush(style.fillBrush);
        if (!batch.inactiveBodies.isEmpty()) {
            painter.setPen(style.inactiveBorderPen);
            painter.drawRects(batch.inactiveBodies.constData(),
                              static_cast<int>(batch.inactiveBodies.size()));
        }
        if (!batch.activeBodies.isEmpty()) {
            painter.setPen(style.activeBorderPen);
            painter.drawRects(batch.activeBodies.constData(),
                              static_cast<int>(batch.activeBodies.size()));
        }
    }

    painter.setBrush(Qt::NoBrush);
    for (const int styleIndex : m_usedStyleIndices) {
        const auto& style = styles[styleIndex];
        const auto& batch = m_noteBatches[styleIndex];
        if (!batch.inactiveAttackLines.isEmpty()) {
            painter.setPen(style.inactiveAttackLinePen);
            painter.drawLines(batch.inactiveAttackLines.constData(),
                              static_cast<int>(batch.inactiveAttackLines.size()));
        }
        if (!batch.activeAttackLines.isEmpty()) {
            painter.setPen(style.activeAttackLinePen);
            painter.drawLines(batch.activeAttackLines.constData(),
                              static_cast<int>(batch.activeAttackLines.size()));
        }
    }
    if (!m_tremoloLines.isEmpty()) {
        painter.setPen(QPen(QColor(255, 255, 255, 110), 1));
        painter.drawLines(m_tremoloLines.constData(), static_cast<int>(m_tremoloLines.size()));
    }
    painter.restore();
}

void FallingNotesRenderer::drawStrikeLine(QPainter& painter, const PlaybackSceneGeometry& geometry,
                                          const PlaybackSceneState& state)
{
    const qreal left = geometry.pianoRect.left();
    const qreal right = geometry.drumRect.isEmpty() ? geometry.pianoRect.right() : geometry.drumRect.right();
    QColor glow = m_theme.strikeLine;
    glow.setAlpha(45);
    painter.fillRect(QRectF(left, geometry.strikeLineY - 5.0, right - left, 10.0), glow);
    painter.setPen(QPen(m_theme.strikeLine, 2));
    painter.drawLine(QPointF(left, geometry.strikeLineY), QPointF(right, geometry.strikeLineY));

    if (!state.chart) return;
    const auto& labelNoteIndices = m_activeNoteLookup.melodicLabelNoteIndices();
    if (labelNoteIndices.isEmpty()) return;
    QFont font = painter.font();
    font.setPointSizeF(10.0);
    font.setWeight(QFont::DemiBold);
    painter.setFont(font);
    painter.setPen(m_theme.primaryText);
    qreal x = left + 8.0;
    for (const int noteIndex : labelNoteIndices) {
        const auto& note = state.chart->notes().at(noteIndex);
        const auto& label = m_textLayoutCache.layout(
            TextLayoutRole::Strike, note.simplifiedLabel, font, -1.0,
            painterDevicePixelRatio(painter));
        const qreal textWidth = label.advance;
        if (x + textWidth > right - 8.0) break;
        const QRectF textRect(x, geometry.strikeLineY + 5.0, textWidth + 1.0, 19.0);
        drawPreparedText(painter, label, textRect, Qt::AlignCenter);
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

void FallingNotesRenderer::drawKeyboardBase(QPainter& painter, const PlaybackSceneGeometry& geometry,
                                            const PlaybackSceneState& state)
{
    painter.fillRect(geometry.keyboardRect, m_theme.keyboardBackground);

    painter.setPen(QPen(m_theme.whiteKeyBorder, 1));
    painter.setBrush(m_theme.whiteKey);
    for (const auto& slot : geometry.pitches) {
        if (!slot.valid || slot.blackKey) continue;
        painter.drawRect(slot.keyRect.adjusted(0.0, 0.0, -0.5, -0.5));
    }
    painter.setPen(QPen(m_theme.blackKeyBorder, 1));
    painter.setBrush(m_theme.blackKey);
    for (const auto& slot : geometry.pitches) {
        if (!slot.valid || !slot.blackKey) continue;
        painter.drawRect(slot.keyRect.adjusted(0.5, 0.0, -0.5, -1.0));
    }

    QFont keyFont = painter.font();
    keyFont.setPointSizeF(7.5);
    painter.setFont(keyFont);
    painter.setPen(QColor(44, 47, 46));
    for (const auto& slot : geometry.pitches) {
        if (!slot.valid || slot.blackKey || slot.pitch % 12 != 0 || slot.keyRect.width() < 12.0) continue;
        const auto& text = m_textLayoutCache.layout(
            TextLayoutRole::Octave, octaveLabelForPitch(slot.pitch),
            keyFont, -1.0, painterDevicePixelRatio(painter));
        drawPreparedText(painter, text, slot.keyRect.adjusted(1.0, 0.0, -1.0, -5.0),
                         Qt::AlignHCenter | Qt::AlignBottom);
    }

    if (state.chart && !geometry.drumRect.isEmpty()) {
        painter.setPen(QPen(QColor(255, 255, 255, 35), 1));
        painter.setBrush(QColor("#292d30"));
        for (const auto& slot : geometry.drumSlots) {
            painter.drawRect(slot.keyRect.adjusted(0.0, 0.0, -0.5, -0.5));
        }
        painter.setPen(m_theme.primaryText);
        for (const auto& slot : geometry.drumSlots) {
            if (slot.lane < state.chart->drumLanes().size() && slot.keyRect.width() >= 18.0) {
                const auto& text = m_textLayoutCache.layout(
                    TextLayoutRole::Drum, state.chart->drumLanes()[slot.lane].name,
                    keyFont, slot.keyRect.width() - 5.0, painterDevicePixelRatio(painter));
                drawPreparedText(painter, text, slot.keyRect.adjusted(3.0, 4.0, -3.0, -4.0),
                                 Qt::AlignHCenter | Qt::AlignBottom);
            }
        }
    }
}

void FallingNotesRenderer::drawActiveKeyboard(QPainter& painter,
                                              const PlaybackSceneGeometry& geometry,
                                              const PlaybackSceneState& state)
{
    if (!state.chart) return;

    const auto styleForNote = [this](int noteIndex) -> const NoteRenderStyle* {
        const auto* style = m_noteRenderCache.styleForNote(noteIndex);
        return style;
    };

    const auto& activePitches = m_activeNoteLookup.activePitches();
    bool paintedActiveWhiteKey = false;
    painter.setPen(QPen(m_theme.whiteKeyBorder, 1));
    for (const int pitch : activePitches) {
        const auto* slot = geometry.pitchSlot(pitch);
        const auto* style = styleForNote(m_activeNoteLookup.noteIndexForPitch(pitch));
        if (!slot || slot->blackKey || !style) continue;
        painter.setBrush(style->activeWhiteKeyBrush);
        painter.drawRect(slot->keyRect.adjusted(0.0, 0.0, -0.5, -0.5));
        paintedActiveWhiteKey = true;
    }

    // White keys extend underneath adjacent black keys. Restore the immutable
    // black-key foreground in one batch before painting active black keys.
    if (paintedActiveWhiteKey && !m_blackKeyForegroundRects.isEmpty()) {
        painter.setPen(QPen(m_theme.blackKeyBorder, 1));
        painter.setBrush(m_theme.blackKey);
        painter.drawRects(m_blackKeyForegroundRects.constData(),
                          static_cast<int>(m_blackKeyForegroundRects.size()));
    }
    painter.setPen(QPen(m_theme.blackKeyBorder, 1));
    for (const int pitch : activePitches) {
        const auto* slot = geometry.pitchSlot(pitch);
        const auto* style = styleForNote(m_activeNoteLookup.noteIndexForPitch(pitch));
        if (!slot || !slot->blackKey || !style) continue;
        painter.setBrush(style->activeBlackKeyBrush);
        painter.drawRect(slot->keyRect.adjusted(0.5, 0.0, -0.5, -1.0));
    }

    QFont keyFont = painter.font();
    keyFont.setPointSizeF(7.5);
    painter.setFont(keyFont);
    painter.setPen(QColor(44, 47, 46));
    for (const int pitch : activePitches) {
        const auto* slot = geometry.pitchSlot(pitch);
        if (!slot || slot->blackKey || pitch % 12 != 0 || slot->keyRect.width() < 12.0) continue;
        const auto& text = m_textLayoutCache.layout(
            TextLayoutRole::Octave, octaveLabelForPitch(pitch),
            keyFont, -1.0, painterDevicePixelRatio(painter));
        drawPreparedText(painter, text, slot->keyRect.adjusted(1.0, 0.0, -1.0, -5.0),
                         Qt::AlignHCenter | Qt::AlignBottom);
    }

    if (geometry.drumRect.isEmpty()) return;
    for (const int lane : m_activeNoteLookup.activeDrumLanes()) {
        const auto* slot = geometry.drumSlot(lane);
        const auto* style = styleForNote(m_activeNoteLookup.noteIndexForDrumLane(lane));
        if (!slot || !style) continue;
        painter.setPen(QPen(QColor(255, 255, 255, 35), 1));
        painter.setBrush(style->activeDrumKeyBrush);
        painter.drawRect(slot->keyRect.adjusted(0.0, 0.0, -0.5, -0.5));
        if (lane < state.chart->drumLanes().size() && slot->keyRect.width() >= 18.0) {
            painter.setPen(m_theme.primaryText);
            const auto& text = m_textLayoutCache.layout(
                TextLayoutRole::Drum, state.chart->drumLanes()[lane].name,
                keyFont, slot->keyRect.width() - 5.0, painterDevicePixelRatio(painter));
            drawPreparedText(painter, text, slot->keyRect.adjusted(3.0, 4.0, -3.0, -4.0),
                             Qt::AlignHCenter | Qt::AlignBottom);
        }
    }
}

void FallingNotesRenderer::drawOverlay(QPainter& painter, const PlaybackSceneGeometry& geometry,
                                       const PlaybackSceneState& state)
{
    if (state.chart) {
        const auto& overlay = m_overlayTimeline.at(state.transportPositionUs);
        const auto& marker = overlay.marker;
        const auto& lyric = overlay.lyric;
        if (!marker.isEmpty() || !lyric.isEmpty()) {
            QFont overlayFont = painter.font();
            overlayFont.setPointSizeF(9.5);
            painter.setFont(overlayFont);
            const qreal dpr = painterDevicePixelRatio(painter);
            if (!marker.isEmpty()) {
                painter.setPen(m_theme.secondaryText);
                const auto& text = m_textLayoutCache.layout(
                    TextLayoutRole::Marker, marker, overlayFont,
                    geometry.pianoRect.width(), dpr);
                drawPreparedText(painter, text,
                    QRectF(geometry.pianoRect.left(), geometry.fallingRect.top() + 8.0,
                           geometry.pianoRect.width(), 24.0),
                    Qt::AlignHCenter | Qt::AlignVCenter);
            }
            if (!lyric.isEmpty()) {
                painter.setPen(m_theme.primaryText);
                const auto& text = m_textLayoutCache.layout(
                    TextLayoutRole::Lyric, lyric, overlayFont,
                    geometry.pianoRect.width() * 0.8, dpr);
                drawPreparedText(painter, text,
                    QRectF(geometry.pianoRect.left(), geometry.strikeLineY - 34.0,
                           geometry.pianoRect.width(), 24.0),
                    Qt::AlignHCenter | Qt::AlignVCenter);
            }
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
