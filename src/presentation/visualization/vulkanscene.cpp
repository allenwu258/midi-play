#include "vulkanscene.h"

#include <QPainter>
#include <algorithm>
#include <cmath>

namespace midi_play::presentation::visualization {
namespace {
std::array<float, 4> rgba(const QColor& c)
{
    return {float(c.redF()), float(c.greenF()), float(c.blueF()), float(c.alphaF())};
}
float seconds(qint64 value, qint64 origin) { return float((value - origin) / 1'000'000.0); }
}

void VulkanScene::prepare(const midi_play::visualization::PlaybackSceneState& state,
                          QSize size, qreal dpr, const QFont& font)
{
    const bool chartChanged = m_chart != state.chart;
    const bool layoutChanged = chartChanged || m_size != size;
    if (chartChanged) {
        m_chart = state.chart;
        m_index = {};
        if (m_chart) m_index.rebuild(m_chart->notes());
        m_window.reset();
        m_overlay.setChart(m_chart);
        m_timeOriginUs = 0;
    }
    if (layoutChanged) {
        m_size = size;
        m_geometry = SceneLayoutEngine().layout(size, m_chart.get(), state.lookAheadUs);
    }
    if (m_atlas.isNull() || m_dpr != dpr || m_atlasFull || m_atlasY > 1536 || chartChanged) {
        m_dpr = dpr;
        m_atlas = QImage(2048, 2048, QImage::Format_RGBA8888_Premultiplied);
        m_atlas.fill(Qt::transparent);
        m_glyphs.clear();
        m_atlasX = m_atlasY = 1;
        m_rowHeight = 0;
        m_atlasFull = false;
        ++m_atlasRevision;
    }
    m_cache.prepare(m_chart, m_geometry);
    const bool candidatesChanged = m_window.ensure(m_index,
        state.transportPositionUs - state.afterglowUs,
        state.transportPositionUs + state.lookAheadUs, state.visibilityGuardUs);
    if (candidatesChanged || layoutChanged) rebuildNotes(state);
    m_active.reset(m_chart ? m_chart->drumLanes().size() : 0);
    m_visibleNoteCount = 0;
    if (m_chart) {
        for (int index : m_window.candidateNoteIndices()) {
            const auto& note = m_chart->notes()[index];
            if (note.startUs > state.transportPositionUs + state.lookAheadUs
                || note.audibleEndUs < state.transportPositionUs - state.afterglowUs) continue;
            ++m_visibleNoteCount;
            if (state.transportState == midi_play::playback::State::Playing
                && note.startUs <= state.transportPositionUs && note.audibleEndUs > state.transportPositionUs)
                m_active.add(index, note);
        }
    }
    buildDecorations(state, font);
}

void VulkanScene::rebuildNotes(const midi_play::visualization::PlaybackSceneState& state)
{
    ++m_notesRevision;
    m_notes.clear();
    if (!m_chart) return;
    // Preserve the renderer's tails/body/attack/tremolo layer ordering.
    for (int kind = 1; kind <= 4; ++kind) {
        for (int index : m_window.candidateNoteIndices()) {
            const auto* note = m_cache.note(index);
            const auto* style = m_cache.styleForNote(index);
            if (!note || !style || !note->validGeometry) continue;
            if (kind == 1 && !note->hasTail) continue;
            if (kind == 4 && !note->tremolo) continue;
            VulkanQuad quad;
            quad.rect = {float(note->left), 0, float(note->width), 0};
            quad.times = {seconds(note->startUs, m_timeOriginUs), seconds(note->keyEndUs, m_timeOriginUs),
                          seconds(note->audibleEndUs, m_timeOriginUs), float(kind)};
            quad.color = rgba(kind == 1 ? style->tailBrush.color() : style->fillBrush.color());
            quad.border = rgba(style->inactiveBorderPen.color());
            quad.activeBorder = rgba(style->activeBorderPen.color());
            quad.options = {1, style->inactiveBorderPen.style() == Qt::DashLine ? 1.f : 0.f, 0, 0};
            m_notes.push_back(quad);
        }
    }
}

void VulkanScene::rect(QVector<VulkanQuad>& output, const QRectF& area, const QColor& fill,
                       const QColor& edge, float width, bool ellipse)
{
    if (area.isEmpty()) return;
    VulkanQuad quad;
    quad.rect = {float(area.x()), float(area.y()), float(area.width()), float(area.height())};
    quad.color = rgba(fill);
    quad.border = quad.activeBorder = rgba(edge);
    quad.options = {width, 0, 0, ellipse ? 1.f : 0.f};
    output.push_back(quad);
}

VulkanScene::Glyph VulkanScene::glyph(const QString& value, const QFont& font, qreal maximumWidth)
{
    const qreal widthLimit = std::min(maximumWidth < 0 ? 2000.0 / m_dpr : maximumWidth, 2000.0 / m_dpr);
    const QString key = font.toString() + QChar(0) + QString::number(widthLimit, 'f', 3) + QChar(0) + value;
    if (const auto it = m_glyphs.constFind(key); it != m_glyphs.cend()) return *it;
    const auto& layout = m_textLayouts.layout(TextLayoutRole::Lyric, value, font, widthLimit, m_dpr);
    const QSize pixels(qCeil(layout.size.width() * m_dpr) + 4, qCeil(layout.size.height() * m_dpr) + 4);
    if (m_atlasX + pixels.width() >= m_atlas.width()) {
        m_atlasX = 1;
        m_atlasY += m_rowHeight + 1;
        m_rowHeight = 0;
    }
    if (m_atlasY + pixels.height() >= m_atlas.height()) {
        m_atlasFull = true;
        return {};
    }
    Glyph result {QRect(QPoint(m_atlasX, m_atlasY), pixels), QSizeF(pixels) / m_dpr};
    QPainter painter(&m_atlas);
    painter.setClipRect(result.pixels);
    painter.translate(result.pixels.topLeft() + QPoint(2, 2));
    painter.scale(m_dpr, m_dpr);
    painter.setFont(font);
    painter.setPen(Qt::white);
    painter.drawStaticText(QPointF(), layout.staticText);
    painter.end();
    m_atlasX += pixels.width() + 1;
    m_rowHeight = std::max(m_rowHeight, pixels.height());
    m_glyphs.insert(key, result);
    ++m_atlasRevision;
    return result;
}

void VulkanScene::text(QVector<VulkanQuad>& output, const QString& value, const QFont& font,
                       const QRectF& area, const QColor& color, Qt::Alignment alignment, qreal maximumWidth)
{
    if (value.isEmpty()) return;
    const Glyph item = glyph(value, font, maximumWidth);
    if (item.pixels.isEmpty()) return;
    qreal x = area.left();
    qreal y = area.top();
    if (alignment & Qt::AlignHCenter) x += (area.width() - item.size.width()) / 2;
    else if (alignment & Qt::AlignRight) x += area.width() - item.size.width();
    if (alignment & Qt::AlignVCenter) y += (area.height() - item.size.height()) / 2;
    else if (alignment & Qt::AlignBottom) y += area.height() - item.size.height();
    rect(output, QRectF(QPointF(x, y), item.size), color);
    auto& quad = output.back();
    quad.options[2] = 1;
    quad.uv = {float(item.pixels.x()) / 2048, float(item.pixels.y()) / 2048,
               float(item.pixels.width()) / 2048, float(item.pixels.height()) / 2048};
}

void VulkanScene::buildDecorations(const midi_play::visualization::PlaybackSceneState& state, const QFont& font)
{
    m_background.clear();
    m_foreground.clear();
    const auto& g = m_geometry;
    for (const auto& pitch : g.pitches) {
        if (pitch.valid && pitch.blackKey)
            rect(m_background, {pitch.keyRect.x(), g.fallingRect.y(), pitch.keyRect.width(), g.fallingRect.height()}, QColor(255,255,255,7));
    }
    const qreal left = g.pianoRect.left();
    const qreal right = g.drumRect.isEmpty() ? g.pianoRect.right() : g.drumRect.right();
    if (m_chart) {
        QFont gridFont(font); gridFont.setPointSizeF(8);
        const auto& lines = m_chart->gridLines();
        auto it = std::lower_bound(lines.cbegin(), lines.cend(), state.transportPositionUs - state.afterglowUs,
            [](const auto& line, qint64 value) { return line.timeUs < value; });
        for (; it != lines.cend() && it->timeUs <= state.transportPositionUs + state.lookAheadUs; ++it) {
            const qreal y = g.strikeLineY - (it->timeUs - state.transportPositionUs) * g.pixelsPerMicrosecond;
            if (y < g.fallingRect.top() || y >= g.fallingRect.bottom()) continue;
            rect(m_background, {left,y,right-left,it->measureStart ? 2.0 : 1.0}, it->measureStart ? m_theme.measureLine : m_theme.beatLine);
            // Use a dedicated primitive kind for moving horizontal lines.
            // The negative kind is independent from the ellipse/texture flags
            // in options and is rasterized with analytic edge coverage.
            m_background.back().times[3] = -1.0f;
            if (it->measureStart) text(m_background, it->measureLabel, gridFont, {4,y-10,left-9,20}, m_theme.subtleText, Qt::AlignRight|Qt::AlignVCenter);
        }
    }
    rect(m_foreground, {left,g.strikeLineY-5,right-left,10}, QColor(244,211,94,45));
    rect(m_foreground, {left,g.strikeLineY-1,right-left,2}, m_theme.strikeLine);
    if (m_chart) {
        QFont labelFont(font); labelFont.setPointSizeF(10); labelFont.setWeight(QFont::DemiBold);
        qreal x = left + 8;
        for (int index : m_active.melodicLabelNoteIndices()) {
            const auto& note = m_chart->notes()[index];
            const auto& layout = m_textLayouts.layout(TextLayoutRole::Strike, note.simplifiedLabel, labelFont, -1, m_dpr);
            const qreal width = layout.advance;
            if (x + width > right - 8) break;
            const QRectF area(x, g.strikeLineY + 5, width + 1, 19);
            text(m_foreground, note.simplifiedLabel, labelFont, area, m_theme.primaryText, Qt::AlignCenter);
            const int dots = std::min(3, std::abs(note.octaveOffset));
            for (int dot = 0; dot < dots; ++dot)
                rect(m_foreground, {area.center().x()-(dots*3.5-1.5)/2+dot*3.5,
                     note.octaveOffset>0 ? area.top()-1.5 : area.bottom()+0.5,2,2},m_theme.primaryText,Qt::transparent,0,true);
            x += width + 13;
        }
    }
    rect(m_foreground, g.keyboardRect, m_theme.keyboardBackground);
    for (bool black : {false, true}) {
        for (const auto& slot : g.pitches) {
            if (!slot.valid || slot.blackKey != black) continue;
            const auto* style = m_cache.styleForNote(m_active.noteIndexForPitch(slot.pitch));
            const QColor color = style ? (black ? style->activeBlackKeyBrush.color() : style->activeWhiteKeyBrush.color())
                                       : (black ? m_theme.blackKey : m_theme.whiteKey);
            rect(m_foreground, slot.keyRect.adjusted(black ? .5 : 0,0,-.5,black ? -1 : -.5),
                 color, black ? m_theme.blackKeyBorder : m_theme.whiteKeyBorder, 1);
        }
    }
    QFont keyFont(font); keyFont.setPointSizeF(7.5);
    for (const auto& slot : g.pitches) {
        if (slot.valid && !slot.blackKey && slot.pitch % 12 == 0 && slot.keyRect.width() >= 12)
            text(m_foreground, QStringLiteral("C%1").arg(slot.pitch/12-1), keyFont,
                 slot.keyRect.adjusted(1,0,-1,-5), QColor(44,47,46), Qt::AlignHCenter|Qt::AlignBottom);
    }
    if (m_chart) {
        for (const auto& slot : g.drumSlots) {
            const auto* style = m_cache.styleForNote(m_active.noteIndexForDrumLane(slot.lane));
            rect(m_foreground,slot.keyRect.adjusted(0,0,-.5,-.5),style ? style->activeDrumKeyBrush.color() : QColor("#292d30"),QColor(255,255,255,35),1);
            if (slot.lane < m_chart->drumLanes().size() && slot.keyRect.width() >= 18)
                text(m_foreground,m_chart->drumLanes()[slot.lane].name,keyFont,slot.keyRect.adjusted(3,4,-3,-4),m_theme.primaryText,Qt::AlignHCenter|Qt::AlignBottom,slot.keyRect.width()-5);
        }
        const auto& overlay = m_overlay.at(state.transportPositionUs);
        QFont overlayFont(font); overlayFont.setPointSizeF(9.5);
        text(m_foreground,overlay.marker,overlayFont,{left,g.fallingRect.top()+8,g.pianoRect.width(),24},m_theme.secondaryText,Qt::AlignCenter,g.pianoRect.width());
        text(m_foreground,overlay.lyric,overlayFont,{left,g.strikeLineY-34,g.pianoRect.width(),24},m_theme.primaryText,Qt::AlignCenter,g.pianoRect.width()*.8);
    }
    if (!m_chart || state.loading || !state.errorMessage.isEmpty()) {
        rect(m_foreground,g.fallingRect,QColor(10,11,12,state.loading ? 118 : 148));
        QFont statusFont(font); statusFont.setPointSizeF(11); statusFont.setWeight(QFont::DemiBold);
        const QString message = !state.errorMessage.isEmpty() ? state.errorMessage : state.loading
            ? QStringLiteral("正在分析音乐文件...") : QStringLiteral("打开 MusicXML 或 MIDI 文件开始播放");
        text(m_foreground,message,statusFont,g.fallingRect.adjusted(24,24,-24,-24),
             state.errorMessage.isEmpty() ? m_theme.primaryText : m_theme.error,Qt::AlignCenter,g.fallingRect.width()-48);
    }
}

} // namespace midi_play::presentation::visualization
