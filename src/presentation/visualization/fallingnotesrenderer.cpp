#include "fallingnotesrenderer.h"
#include "rasterrenderpolicy.h"

#include <QPainter>
#include <QRadialGradient>

#include <algorithm>
#include <array>
#include <cmath>

namespace midi_play::presentation::visualization {
namespace {

using midi_play::visualization::PlaybackSceneState;

qreal yForTime(const PlaybackSceneGeometry& geometry, qint64 timeUs, qint64 positionUs)
{
    return geometry.strikeLineY
        - static_cast<qreal>(timeUs - positionUs) * geometry.pixelsPerMicrosecond;
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
    const PlaybackSceneState& state, const QImage& background)
{
    prepareScene(geometry, state);
    painter.save();
    RasterRenderPolicy::apply(painter);
    painter.fillRect(geometry.bounds, m_theme.background);
    if (!background.isNull() && !geometry.fallingRect.isEmpty()) {
        painter.save();
        painter.setClipRect(geometry.fallingRect);
        const QSizeF sourceSize = background.size();
        const QSizeF targetSize = geometry.fallingRect.size();
        const qreal scale = std::max(targetSize.width() / sourceSize.width(),
                                     targetSize.height() / sourceSize.height());
        const QSizeF fitted = sourceSize * scale;
        const QRectF target(geometry.fallingRect.center() - QPointF(fitted.width(), fitted.height()) / 2,
                            fitted);
        painter.drawImage(target, background);
        painter.fillRect(geometry.fallingRect, QColor(0, 0, 0, 85));
        painter.restore();
    }
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
    m_noteFrame.prepare(state, geometry, m_noteRenderCache);
    m_activeNoteLookup = m_noteFrame.active();
    m_visibleNoteCount = m_noteFrame.visibleCount();
    m_activeNoteCount = m_noteFrame.activeCount();
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
    m_theme = theme::themeFor(state.themeMode).visualization;
    m_noteRenderCache.prepare(state.chart, geometry, state.themeMode, state.noteColorMode);
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
}

void FallingNotesRenderer::drawPitchBands(QPainter& painter,
                                          const PlaybackSceneGeometry& geometry) const
{
    if (m_pitchBandRects.isEmpty()) return;
    painter.save();
    painter.setClipRect(geometry.fallingRect);
    painter.setPen(Qt::NoPen);
    painter.setBrush(m_theme.pitchBand);
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
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    painter.setPen(Qt::NoPen);
    m_noteRasterCache.prepare(m_noteRenderCache, painterDevicePixelRatio(painter));
    // Preserve chronological order within each material layer. Source-over
    // blending is not commutative, so style-based reordering is incorrect.
    for (int layer = 1; layer <= 4; ++layer) {
        // Body textures already contain edge coverage; avoid a second AA pass.
        painter.setRenderHint(QPainter::Antialiasing, layer != 2);
        for (const int index : state.candidateNoteIndices) {
            const auto* note = m_noteRenderCache.note(index);
            const auto* style = m_noteRenderCache.styleForNote(index);
            if (!note || !style || !note->validGeometry) continue;
            const auto shape = noteGeometry(*note, geometry, state.transportPositionUs);
            if (shape.opacity <= 0) continue;
            painter.setPen(Qt::NoPen);
            painter.setOpacity(shape.opacity * (layer <= 2 ? m_noteFrame.bodyOpacity() : 1));
            if (layer == 1 && note->hasTail && shape.tail.intersects(geometry.fallingRect)) {
                painter.fillRect(shape.tail, style->tailGradientBrush);
            } else if (layer == 2 && shape.body.intersects(geometry.fallingRect)) {
                m_noteRasterCache.drawBody(painter, shape.body, *style, note->styleIndex);
            } else if (layer == 3 && shape.head.intersects(geometry.fallingRect)) {
                painter.fillRect(shape.head, style->material.head);
            } else if (layer == 4 && note->tremolo && shape.body.height() > 14 && note->width > 6) {
                const qreal y = shape.body.center().y();
                painter.setPen(QPen(m_theme.tremolo, 1.2));
                painter.drawLine(QPointF(shape.body.left() + 3, y + 3),
                                 QPointF(shape.body.right() - 3, y - 3));
            }
        }
    }
    painter.restore();
}

void FallingNotesRenderer::drawStrikeLine(QPainter& painter, const PlaybackSceneGeometry& geometry,
                                          const PlaybackSceneState& state)
{
    const qreal left = geometry.pianoRect.left();
    const qreal right = geometry.drumRect.isEmpty() ? geometry.pianoRect.right() : geometry.drumRect.right();
    painter.save();
    painter.setClipRect(geometry.fallingRect);
    if (!geometry.notationStripRect.isEmpty()) {
        const QColor glow = m_theme.strikeGlow;
        painter.fillRect(QRectF(left, geometry.strikeLineY - 3.0, right - left, 6.0), glow);
        QColor line = m_theme.strikeLine;
        line.setAlpha(145);
        painter.setPen(QPen(line, 1));
        painter.drawLine(QPointF(left, geometry.strikeLineY), QPointF(right, geometry.strikeLineY));
    }

    painter.setPen(Qt::NoPen);
    for (const auto& light : m_noteFrame.glows()) {
        painter.save();
        painter.translate(light.rect.center());
        painter.scale(light.rect.width() * 0.5, light.rect.height() * 0.5);
        QRadialGradient gradient(0, 0, 1);
        gradient.setColorAt(0, light.color);
        QColor transparent = light.color;
        transparent.setAlpha(0);
        gradient.setColorAt(1, transparent);
        painter.setBrush(gradient);
        painter.drawRect(QRectF(-1, -1, 2, 2));
        painter.restore();
    }
    painter.restore();

    if (!state.chart || geometry.notationStripRect.isEmpty()) return;
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
    painter.setPen(m_theme.keyText);
    for (const auto& slot : geometry.pitches) {
        if (!slot.valid || slot.blackKey || slot.pitch % 12 != 0 || slot.keyRect.width() < 12.0) continue;
        const auto& text = m_textLayoutCache.layout(
            TextLayoutRole::Octave, octaveLabelForPitch(slot.pitch),
            keyFont, -1.0, painterDevicePixelRatio(painter));
        drawPreparedText(painter, text, slot.keyRect.adjusted(1.0, 0.0, -1.0, -5.0),
                         Qt::AlignHCenter | Qt::AlignBottom);
    }

    if (state.chart && !geometry.drumRect.isEmpty()) {
        painter.setPen(QPen(m_theme.drumKeyBorder, 1));
        painter.setBrush(m_theme.drumKey);
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

    QVector<int> activePitches;
    for (int pitch = 0; pitch < 128; ++pitch) {
        if (m_noteFrame.key(pitch).noteIndex >= 0) activePitches.push_back(pitch);
    }
    bool paintedActiveWhiteKey = false;
    painter.setPen(QPen(m_theme.whiteKeyBorder, 1));
    for (const int pitch : activePitches) {
        const auto* slot = geometry.pitchSlot(pitch);
        const auto& light = m_noteFrame.key(pitch);
        const auto* style = styleForNote(light.noteIndex);
        if (!slot || slot->blackKey || !style) continue;
        painter.setBrush(illuminatedKeyColor(m_theme.whiteKey, style->material.keyFill, light.strength * 0.60));
        painter.drawRect(slot->keyRect.adjusted(0.0, 0.0, -0.5, -0.5));
        QColor top = style->material.keyTop;
        top.setAlphaF(light.strength);
        painter.fillRect(QRectF(slot->keyRect.left(), slot->keyRect.top(), slot->keyRect.width() - 0.5, 5), top);
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
        const auto& light = m_noteFrame.key(pitch);
        const auto* style = styleForNote(light.noteIndex);
        if (!slot || !slot->blackKey || !style) continue;
        painter.setBrush(illuminatedKeyColor(m_theme.blackKey, style->material.keyFill, light.strength * 0.70));
        painter.drawRect(slot->keyRect.adjusted(0.5, 0.0, -0.5, -1.0));
        QColor top = style->material.keyTop;
        top.setAlphaF(light.strength);
        painter.fillRect(QRectF(slot->keyRect.left() + 0.5, slot->keyRect.top(), slot->keyRect.width() - 1, 4), top);
    }

    QFont keyFont = painter.font();
    keyFont.setPointSizeF(7.5);
    painter.setFont(keyFont);
    painter.setPen(m_theme.keyText);
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
    for (int lane = 0; lane < geometry.drumSlots.size(); ++lane) {
        const auto* slot = geometry.drumSlot(lane);
        const auto& light = m_noteFrame.drum(lane);
        const auto* style = styleForNote(light.noteIndex);
        if (!slot || !style) continue;
        painter.setPen(QPen(m_theme.drumKeyBorder, 1));
        painter.setBrush(illuminatedKeyColor(m_theme.drumKey, style->material.keyFill, light.strength * 0.70));
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
    const QColor veil = state.loading ? m_theme.loadingVeil : m_theme.emptyVeil;
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
