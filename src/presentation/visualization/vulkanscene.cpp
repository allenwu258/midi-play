#include "vulkanscene.h"
#include "backgroundimageplacement.h"

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

VulkanEffectsProfile effectsProfileFor(midi_play::settings::ThemeMode mode, bool enabled,
                                       midi_play::settings::VisualEffectLevel level)
{
    if (!enabled) return {};
    const bool light = mode == midi_play::settings::ThemeMode::Light;
    const auto base = light
        ? VulkanEffectsProfile {0.20f, 0.58f, 0.22f, 0.20f, 0.34f, 0.52f}
        : VulkanEffectsProfile {0.34f, 0.82f, 0.34f, 0.32f, 0.58f, 0.80f};
    const float scale = level == midi_play::settings::VisualEffectLevel::Low
        ? 0.50f
        : level == midi_play::settings::VisualEffectLevel::Medium ? 0.75f : 1.0f;
    return {base.noteHaloStrength * scale, base.noteEdgeStrength * scale,
            base.noteSheenStrength * scale, base.strikeGlowStrength * scale,
            base.keyGlowStrength * scale, base.particleStrength * scale};
}
}

void VulkanScene::prepare(const midi_play::visualization::PlaybackSceneState& state,
                          QSize size, qreal dpr, const QFont& font,
                          bool hasBackground, QSize backgroundSize, quint64 backgroundRevision)
{
    const bool chartChanged = m_chart != state.chart;
    const auto mode = midi_play::settings::normalizeThemeMode(state.themeMode);
    const bool themeChanged = m_themeMode != mode;
    const auto effectLevel = midi_play::settings::normalizeVisualEffectLevel(state.visualEffectsLevel);
    const bool effectsChanged = m_visualEffectsEnabled != state.visualEffectsEnabled
        || m_visualEffectsLevel != effectLevel;
    m_themeMode = mode;
    m_theme = theme::themeFor(mode).visualization;
    m_visualEffectsEnabled = state.visualEffectsEnabled;
    m_visualEffectsLevel = effectLevel;
    m_effectsProfile = effectsProfileFor(mode, state.visualEffectsEnabled, effectLevel);
    const bool layoutChanged = chartChanged || m_size != size || m_lookAheadUs != state.lookAheadUs
        || m_showNotationStrip != state.showNotationStrip;
    const bool backgroundChanged = m_hasBackground != hasBackground
        || (hasBackground && m_backgroundSize != backgroundSize)
        || (hasBackground && m_backgroundRevision != backgroundRevision)
        || m_backgroundAlignment != midi_play::settings::normalizeBackgroundImageAlignment(
            state.backgroundImageAlignment)
        || m_backgroundOpacity != std::clamp(state.backgroundImageOpacity, 0, 100);
    m_hasBackground = hasBackground;
    m_backgroundSize = backgroundSize;
    m_backgroundRevision = backgroundRevision;
    m_backgroundAlignment = midi_play::settings::normalizeBackgroundImageAlignment(
        state.backgroundImageAlignment);
    m_backgroundOpacity = std::clamp(state.backgroundImageOpacity, 0, 100);
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
        m_lookAheadUs = state.lookAheadUs;
        m_showNotationStrip = state.showNotationStrip;
        m_geometry = SceneLayoutEngine().layout(size, m_chart.get(),
                                                state.lookAheadUs, state.showNotationStrip);
    }
    if (layoutChanged || themeChanged || backgroundChanged) rebuildStaticUi();
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
    m_cache.prepare(m_chart, m_geometry, mode, state.noteColorMode);
    const bool materialsChanged = m_consumedMaterialRevision != m_cache.materialRevision();
    const bool candidatesChanged = m_window.ensure(m_index,
        state.transportPositionUs - state.afterglowUs,
        state.transportPositionUs + state.lookAheadUs, state.visibilityGuardUs);
    if (candidatesChanged || layoutChanged || materialsChanged || effectsChanged) {
        rebuildNotes(state);
        m_consumedMaterialRevision = m_cache.materialRevision();
    }
    auto frameState = state;
    const auto& indices = m_window.candidateNoteIndices();
    frameState.candidateNoteIndices = {indices.constData(), size_t(indices.size())};
    frameState.updateVisibleWindow();
    m_noteFrame.prepare(frameState, m_geometry, m_cache);
    m_active = m_noteFrame.active();
    m_visibleNoteCount = m_noteFrame.visibleCount();
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
            quad.color = rgba(kind == 4 ? m_theme.tremolo
                : kind == 1 ? style->material.tail : style->material.body);
            quad.activeBorder = rgba(style->material.head);
            const float phase = float((note->instanceId % 4096u) * 0.000244140625);
            // options.x is the halo padding in logical pixels for note
            // primitives. The vertex shader ignores it as a stroke width for
            // notes, while UI quads continue to use it as their border width.
            quad.options = {6, (note->flags & midi_play::visualization::GhostNote) ? 1.f : 0.f,
                            state.visualEffectsEnabled ? 1.f : 0.f, phase};
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

void VulkanScene::rebuildStaticUi()
{
    ++m_staticUiRevision;
    m_staticUi.clear();
    auto& output = m_staticUi.quads;
    const auto& g = m_geometry;
    m_staticUi.beginLayer(VulkanUiLayer::Background);
    if (m_hasBackground) {
        const auto placement = calculateBackgroundImagePlacement(
            m_backgroundSize, g.fallingRect, m_backgroundAlignment);
        const qreal alpha = m_backgroundOpacity / 100.0;
        rect(output, placement.destination, QColor(255, 255, 255, qRound(255.0 * alpha)));
        auto& background = output.back();
        background.options[2] = 2;
        background.uv = {float(placement.sourceUv.left()), float(placement.sourceUv.top()),
                         float(placement.sourceUv.width()), float(placement.sourceUv.height())};
        rect(output, placement.destination, QColor(0, 0, 0, qRound(85.0 * alpha)));
    }
    for (const auto& pitch : g.pitches) {
        if (pitch.valid && pitch.blackKey)
            rect(output, {pitch.keyRect.x(), g.fallingRect.y(), pitch.keyRect.width(), g.fallingRect.height()}, m_theme.pitchBand);
    }
    m_staticUi.beginLayer(VulkanUiLayer::Strike);
    m_staticUi.beginLayer(VulkanUiLayer::WhiteKeys);
    rect(output, g.keyboardRect, m_theme.keyboardBackground);
    for (bool black : {false, true}) {
        if (black) m_staticUi.beginLayer(VulkanUiLayer::BlackKeys);
        if (!black) for (const auto& slot : g.leftKeyboardExtensionPitches) {
            if (!slot.valid) continue;
            rect(output, slot.keyRect.adjusted(0, 0, -.5, -.5),
                 m_theme.whiteKey, m_theme.whiteKeyBorder, 1);
        }
        for (const auto& slot : g.pitches) {
            if (!slot.valid || slot.blackKey != black) continue;
            rect(output, slot.keyRect.adjusted(black ? .5 : 0, 0, -.5, black ? -1 : -.5),
                 black ? m_theme.blackKey : m_theme.whiteKey,
                 black ? m_theme.blackKeyBorder : m_theme.whiteKeyBorder, 1);
        }
        if (!black) for (const auto& slot : g.drumSlots)
            rect(output, slot.keyRect.adjusted(0, 0, -.5, -.5), m_theme.drumKey, m_theme.drumKeyBorder, 1);
    }
    m_staticUi.beginLayer(VulkanUiLayer::Labels);
    m_staticUi.beginLayer(VulkanUiLayer::Overlay);
    m_staticUi.finish();
}

void VulkanScene::buildDecorations(const midi_play::visualization::PlaybackSceneState& state, const QFont& font)
{
    m_dynamicUi.clear();
    auto& output = m_dynamicUi.quads;
    const auto& g = m_geometry;
    m_dynamicUi.beginLayer(VulkanUiLayer::Background);
    const qreal left = g.pianoRect.left();
    const qreal effectLeft = g.strikeLineLeft();
    const qreal right = g.drumRect.isEmpty() ? g.pianoRect.right() : g.drumRect.right();
    if (state.visualEffectsEnabled && !g.fallingRect.isEmpty()) {
        QColor atmosphere = m_theme.strikeLine;
        atmosphere.setAlphaF(0.045);
        rect(output, QRectF(effectLeft, g.fallingRect.top() + g.fallingRect.height() * 0.04,
                            right - effectLeft, g.fallingRect.height() * 0.90),
             atmosphere, Qt::transparent, 0, true);
        output.back().options[3] = 2;
    }
    if (m_chart) {
        QFont gridFont(font); gridFont.setPointSizeF(8);
        const auto& lines = m_chart->gridLines();
        auto it = std::lower_bound(lines.cbegin(), lines.cend(), state.transportPositionUs - state.afterglowUs,
            [](const auto& line, qint64 value) { return line.timeUs < value; });
        for (; it != lines.cend() && it->timeUs <= state.transportPositionUs + state.lookAheadUs; ++it) {
            const qreal y = g.strikeLineY - (it->timeUs - state.transportPositionUs) * g.pixelsPerMicrosecond;
            if (y < g.fallingRect.top() || y >= g.fallingRect.bottom()) continue;
            rect(output, {effectLeft,y,right-effectLeft,it->measureStart ? 2.0 : 1.0}, it->measureStart ? m_theme.measureLine : m_theme.beatLine);
            // Use a dedicated primitive kind for moving horizontal lines.
            // The negative kind is independent from the ellipse/texture flags
            // in options and is rasterized with analytic edge coverage.
            output.back().times[3] = -1.0f;
            if (it->measureStart) text(output, it->measureLabel, gridFont, {4,y-10,left-9,20}, m_theme.subtleText, Qt::AlignRight|Qt::AlignVCenter);
        }
    }
    m_dynamicUi.beginLayer(VulkanUiLayer::Strike);
    const bool showStrikeLine = !g.notationStripRect.isEmpty() || state.visualEffectsEnabled;
    if (showStrikeLine) {
        const qreal strikeLeft = g.strikeLineLeft();
        const qreal strikeWidth = right - strikeLeft;
        if (state.visualEffectsEnabled) {
            const qreal pulse = 0.5 + 0.5 * std::sin(
                qreal(state.transportPositionUs) * 0.0000075);
            QColor ambient = m_theme.strikeGlow;
            ambient.setAlphaF(0.075 + 0.09 * pulse * m_effectsProfile.strikeGlowStrength);
            rect(output, {strikeLeft, g.strikeLineY - 13.0, strikeWidth, 26.0}, ambient);
            QColor beam = m_theme.strikeGlow;
            beam.setAlphaF(0.16 + 0.20 * m_effectsProfile.strikeGlowStrength);
            rect(output, {strikeLeft, g.strikeLineY - 4.0, strikeWidth, 8.0}, beam);
            QColor strike = m_theme.strikeLine;
            strike.setAlphaF(0.70 + 0.18 * pulse);
            rect(output, {strikeLeft, g.strikeLineY - 0.9, strikeWidth, 1.8}, strike);
            for (const auto& slot : g.pitches) {
                if (!slot.valid) continue;
                const auto& light = m_noteFrame.key(slot.pitch);
                if (light.noteIndex < 0 || light.strength <= 0.02) continue;
                const auto* style = m_cache.styleForNote(light.noteIndex);
                if (!style) continue;
                QColor segment = style->material.glow;
                segment.setAlphaF(std::min<qreal>(0.84,
                    light.strength * (0.68 + m_effectsProfile.strikeGlowStrength)));
                rect(output, {slot.centerX - slot.noteWidth * 0.95,
                              g.strikeLineY - 2.4, slot.noteWidth * 1.9, 4.8}, segment);
            }
        } else {
            rect(output, {strikeLeft,g.strikeLineY-3,strikeWidth,6}, m_theme.strikeGlow);
            QColor strike = m_theme.strikeLine;
            strike.setAlpha(145);
            rect(output, {strikeLeft,g.strikeLineY-0.5,strikeWidth,1}, strike);
        }
    }
    if (state.visualEffectsEnabled) {
        for (const auto& glow : m_noteFrame.glows()) {
            rect(output, glow.rect, glow.color);
            output.back().options[3] = 2;
        }
    }
    if (state.visualEffectsEnabled
        && state.transportState == midi_play::playback::State::Playing) {
        int particleCount = 0;
        const auto addParticles = [&](const KeyIllumination& light, qreal centerX) {
            if (light.noteIndex < 0 || light.attack <= 0.04 || particleCount >= 96) return;
            const auto* style = m_cache.styleForNote(light.noteIndex);
            if (!style) return;
            QColor particle = style->material.glow;
            particle.setAlphaF(std::min<qreal>(0.42,
                light.attack * 0.52 * m_effectsProfile.particleStrength));
            const qreal travel = (1.0 - std::sqrt(light.attack)) * 18.0;
            for (const int direction : {-1, 1}) {
                if (particleCount >= 96) break;
                const qreal x = centerX + direction * travel * 0.42;
                const qreal y = g.strikeLineY - 5.0 - travel;
                rect(output, {x - 2.4, y - 2.4, 4.8, 4.8}, particle,
                     Qt::transparent, 0, true);
                output.back().options[3] = 2;
                ++particleCount;
                if (particleCount >= 96) break;
                QColor streak = particle;
                streak.setAlphaF(particle.alphaF() * 0.72);
                rect(output, {x - 1.0, y - 5.0, 2.0, 10.0}, streak,
                     Qt::transparent, 0, true);
                output.back().options[3] = 2;
                ++particleCount;
            }
        };
        for (const auto& slot : g.pitches) {
            if (!slot.valid) continue;
            addParticles(m_noteFrame.key(slot.pitch), slot.centerX);
        }
        for (const auto& slot : g.drumSlots)
            addParticles(m_noteFrame.drum(slot.lane), slot.centerX);
    }
    if (m_chart && !g.notationStripRect.isEmpty()) {
        QFont labelFont(font); labelFont.setPointSizeF(10); labelFont.setWeight(QFont::DemiBold);
        qreal x = left + 8;
        for (int index : m_active.melodicLabelNoteIndices()) {
            const auto& note = m_chart->notes()[index];
            const auto& layout = m_textLayouts.layout(TextLayoutRole::Strike, note.simplifiedLabel, labelFont, -1, m_dpr);
            const qreal width = layout.advance;
            if (x + width > right - 8) break;
            const QRectF area(x, g.strikeLineY + 5, width + 1, 19);
            text(output, note.simplifiedLabel, labelFont, area, m_theme.primaryText, Qt::AlignCenter);
            const int dots = std::min(3, std::abs(note.octaveOffset));
            for (int dot = 0; dot < dots; ++dot)
                rect(output, {area.center().x()-(dots*3.5-1.5)/2+dot*3.5,
                     note.octaveOffset>0 ? area.top()-1.5 : area.bottom()+0.5,2,2},m_theme.primaryText,Qt::transparent,0,true);
            x += width + 13;
        }
    }
    for (bool black : {false, true}) {
        m_dynamicUi.beginLayer(black ? VulkanUiLayer::BlackKeys : VulkanUiLayer::WhiteKeys);
        for (const auto& slot : g.pitches) {
            if (!slot.valid || slot.blackKey != black) continue;
            const auto& light = m_noteFrame.key(slot.pitch);
            const auto* style = m_cache.styleForNote(light.noteIndex);
            if (!style) continue;
            const QColor base = black ? m_theme.blackKey : m_theme.whiteKey;
            const QColor color = illuminatedKeyColor(base, style->material.keyFill,
                light.strength * (black ? 0.70 : 0.60));
            rect(output, slot.keyRect.adjusted(black ? .5 : 0,0,-.5,black ? -1 : -.5),
                  color, black ? m_theme.blackKeyBorder : m_theme.whiteKeyBorder, 1);
            if (state.visualEffectsEnabled) {
                QColor wash = style->material.glow;
                wash.setAlphaF(std::min<qreal>(0.46,
                    light.strength * m_effectsProfile.keyGlowStrength));
                const qreal spread = black ? 0.16 : 0.24;
                rect(output, slot.keyRect.adjusted(-slot.keyRect.width() * spread, -16,
                    slot.keyRect.width() * spread, 6), wash,
                    Qt::transparent, 0, true);
                output.back().options[3] = 2;
            }
            QColor top = style->material.keyTop;
            top.setAlphaF(light.strength);
            rect(output, {slot.keyRect.left() + (black ? 0.5 : 0), slot.keyRect.top(),
                slot.keyRect.width() - (black ? 1 : 0.5), black ? 4.0 : 5.0}, top);
            if (state.visualEffectsEnabled) {
                QColor reflection = illuminatedKeyColor(style->material.keyTop,
                                                        QColor(255, 255, 255), 0.38);
                reflection.setAlphaF(light.strength * (black ? 0.28 : 0.22));
                rect(output, {slot.keyRect.left() + (black ? 0.5 : 0), slot.keyRect.top(),
                     slot.keyRect.width() - (black ? 1 : 0.5), black ? 1.2 : 1.5}, reflection);
            }
        }
        if (!black) for (const auto& slot : g.drumSlots) {
            const auto& light = m_noteFrame.drum(slot.lane);
            const auto* style = m_cache.styleForNote(light.noteIndex);
            if (style) rect(output, slot.keyRect.adjusted(0,0,-.5,-.5),
                illuminatedKeyColor(m_theme.drumKey, style->material.keyFill, light.strength * 0.70),
                m_theme.drumKeyBorder, 1);
        }
    }
    // Text stays with the atlas-dependent batch. Atlas recycling does not
    // invalidate the static keyboard's geometry or trigger another upload.
    m_dynamicUi.beginLayer(VulkanUiLayer::Labels);
    QFont keyFont(font); keyFont.setPointSizeF(7.5);
    for (const auto& slot : g.pitches) {
        if (slot.valid && !slot.blackKey && slot.pitch % 12 == 0 && slot.keyRect.width() >= 12)
            text(output, QStringLiteral("C%1").arg(slot.pitch/12-1), keyFont,
                 slot.keyRect.adjusted(1,0,-1,-5), m_theme.keyText, Qt::AlignHCenter|Qt::AlignBottom);
    }
    if (m_chart) {
        for (const auto& slot : g.drumSlots) {
            if (slot.lane < m_chart->drumLanes().size() && slot.keyRect.width() >= 18)
                text(output,m_chart->drumLanes()[slot.lane].name,keyFont,slot.keyRect.adjusted(3,4,-3,-4),m_theme.primaryText,Qt::AlignHCenter|Qt::AlignBottom,slot.keyRect.width()-5);
        }
    }
    m_dynamicUi.beginLayer(VulkanUiLayer::Overlay);
    if (m_chart) {
        const auto& overlay = m_overlay.at(state.transportPositionUs);
        QFont overlayFont(font); overlayFont.setPointSizeF(9.5);
        text(output,overlay.marker,overlayFont,{left,g.fallingRect.top()+8,g.pianoRect.width(),24},m_theme.secondaryText,Qt::AlignCenter,g.pianoRect.width());
        text(output,overlay.lyric,overlayFont,{left,g.strikeLineY-34,g.pianoRect.width(),24},m_theme.primaryText,Qt::AlignCenter,g.pianoRect.width()*.8);
    }
    if (!m_chart || state.loading || !state.errorMessage.isEmpty()) {
        rect(output,g.fallingRect,state.loading ? m_theme.loadingVeil : m_theme.emptyVeil);
        QFont statusFont(font); statusFont.setPointSizeF(11); statusFont.setWeight(QFont::DemiBold);
        const QString message = !state.errorMessage.isEmpty() ? state.errorMessage : state.loading
            ? QStringLiteral("正在分析音乐文件...") : QStringLiteral("打开 MusicXML 或 MIDI 文件开始播放");
        text(output,message,statusFont,g.fallingRect.adjusted(24,24,-24,-24),
             state.errorMessage.isEmpty() ? m_theme.primaryText : m_theme.error,Qt::AlignCenter,g.fallingRect.width()-48);
    }
    m_dynamicUi.finish();
}

} // namespace midi_play::presentation::visualization
