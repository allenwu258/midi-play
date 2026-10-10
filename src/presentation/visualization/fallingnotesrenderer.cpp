// QPainter 后端实现：复用共享材质与帧求值，在逻辑像素坐标中组合静态/动态层。
#include "fallingnotesrenderer.h"
#include "backgroundimageplacement.h"
#include "rasterrenderpolicy.h"

#include <QPainter>
#include <QRadialGradient>

#include <algorithm>
#include <array>
#include <cmath>

namespace midi_play::presentation::visualization {
namespace {

using midi_play::visualization::PlaybackSceneState;

// 将音乐时间映射到击键线之上的垂直坐标，未来事件向上偏移。
qreal yForTime(const PlaybackSceneGeometry& geometry, qint64 timeUs, qint64 positionUs)
{
    return geometry.strikeLineY
        - static_cast<qreal>(timeUs - positionUs) * geometry.pixelsPerMicrosecond;
}

// 从实际绘制设备取得 DPR，兼容窗口与离屏 QImage。
qreal painterDevicePixelRatio(const QPainter& painter)
{
    return painter.device() ? painter.device()->devicePixelRatioF() : 1.0;
}

// 一次生成 MIDI 音高对应的八度标签；非法音高返回稳定的空字符串引用。
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

// 根据预排版文字尺寸计算对齐位置，避免每帧重复执行文字布局。
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

// 完整绘制入口：静态底层先于动态音符、击键高亮和状态覆盖层。
void FallingNotesRenderer::render(QPainter& painter, const PlaybackSceneGeometry& geometry,
                                  const PlaybackSceneState& state)
{
    renderStaticLayer(painter, geometry, state);
    renderDynamicLayer(painter, geometry, state);
}

// 组合无需随播放位置变化的背景和键盘，供直接绘制或离屏缓存使用。
void FallingNotesRenderer::renderStaticLayer(QPainter& painter, const PlaybackSceneGeometry& geometry,
                                             const PlaybackSceneState& state)
{
    renderStaticBackgroundLayer(painter, geometry, state);
    renderStaticKeyboardLayer(painter, geometry, state);
}

// 先填充主题背景，再按统一裁切规则绘图和音高条带；保存并恢复调用方画笔状态。
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
        const auto placement = calculateBackgroundImagePlacement(
            background.size(), geometry.fallingRect, state.backgroundImageAlignment);
        if (!placement.destination.isEmpty()) {
            const int opacity = std::clamp(state.backgroundImageOpacity, 0, 100);
            const qreal alpha = opacity / 100.0;
            painter.setOpacity(alpha);
            painter.drawImage(placement.destination, background, QRectF(
                placement.sourceUv.left() * background.width(),
                placement.sourceUv.top() * background.height(),
                placement.sourceUv.width() * background.width(),
                placement.sourceUv.height() * background.height()));
            // 遮罩强度随图片透明度变化，降低背景对音符辨识度的影响。
            QColor mask(0, 0, 0, qRound(85.0 * alpha));
            painter.setOpacity(1.0);
            painter.fillRect(placement.destination, mask);
        }
        painter.restore();
    }
    drawPitchBands(painter, geometry);
    painter.restore();
}

// 绘制键盘底色和固定标签，不包含当前帧的活动键高亮。
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

// 一次求值当前帧，按网格、音符、击键线、键盘高亮、覆盖文本顺序绘制。
void FallingNotesRenderer::renderDynamicLayer(QPainter& painter, const PlaybackSceneGeometry& geometry,
                                              const PlaybackSceneState& state)
{
    prepareScene(geometry, state);
    // 同一帧求值同时提供音符密度、击键光效与活动键，保证表现一致。
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

// 同步图表、主题和材质；只有几何版本变化时重建批量键盘矩形。
void FallingNotesRenderer::prepareScene(const PlaybackSceneGeometry& geometry,
                                        const PlaybackSceneState& state)
{
    m_overlayTimeline.setChart(state.chart);
    m_theme = theme::themeFor(state.themeMode).visualization;
    m_noteRenderCache.prepare(state.chart, geometry, state.themeMode, state.noteColorMode);
    // 几何版本未变化时保留批量矩形；主题变化只影响绘制颜色。
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

// 在下落区域内批量绘制黑键音高条带，不覆盖键盘或区域外内容。
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

// 二分定位可见时间窗起点，绘制拍线及小节标签，避免扫描整曲网格。
void FallingNotesRenderer::drawTimeGrid(QPainter& painter, const PlaybackSceneGeometry& geometry,
                                        const PlaybackSceneState& state)
{
    painter.save();
    painter.setClipRect(geometry.fallingRect);
    const qreal gridLeft = geometry.strikeLineLeft();
    const qreal labelRight = geometry.pianoRect.left();
    const qreal gridRight = geometry.drumRect.isEmpty() ? geometry.pianoRect.right() : geometry.drumRect.right();
    if (!state.chart) {
        painter.restore();
        return;
    }

    const qint64 windowStart = state.transportPositionUs - state.afterglowUs;
    const qint64 windowEnd = state.transportPositionUs + state.lookAheadUs;
    const auto& lines = state.chart->gridLines();
    // 网格按时间排序，先跳过窗口左侧事件，再顺序遍历窗口内部分。
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
            drawPreparedText(painter, text, QRectF(4.0, y - 10.0,
                                                    std::max<qreal>(0.0, labelRight - 9.0), 20.0),
                             Qt::AlignRight | Qt::AlignVCenter);
        }
    }
    painter.restore();
}

// 候选时间窗经几何相交再筛选；按尾、主体、音头、震音四层统一合成。
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
    // Source-over 混合与绘制顺序有关；每个材质层内必须保留音符时间顺序。
    for (int layer = 1; layer <= 4; ++layer) {
        // 主体纹理已包含边缘覆盖率，该层关闭额外抗锯齿以避免重复处理。
        painter.setRenderHint(QPainter::Antialiasing, layer != 2);
        for (const int index : state.candidateNoteIndices) {
            const auto* note = m_noteRenderCache.note(index);
            const auto* style = m_noteRenderCache.styleForNote(index);
            if (!note || !style || !note->validGeometry) continue;
            const auto shape = noteGeometry(*note, geometry, state.transportPositionUs);
            if (shape.opacity <= 0) continue;
            painter.setPen(Qt::NoPen);
            // 密度透明度只作用于尾部和主体，音头及震音标记保持辨识度。
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

// 绘制击键基线、共享帧求值产生的光效及可选简谱提示。
void FallingNotesRenderer::drawStrikeLine(QPainter& painter, const PlaybackSceneGeometry& geometry,
                                          const PlaybackSceneState& state)
{
    const qreal left = geometry.pianoRect.left();
    const qreal right = geometry.drumRect.isEmpty() ? geometry.pianoRect.right() : geometry.drumRect.right();
    painter.save();
    painter.setClipRect(geometry.fallingRect);
    if (!geometry.notationStripRect.isEmpty()) {
        const qreal strikeLeft = geometry.strikeLineLeft();
        const QColor glow = m_theme.strikeGlow;
        painter.fillRect(QRectF(strikeLeft, geometry.strikeLineY - 3.0, right - strikeLeft, 6.0), glow);
        QColor line = m_theme.strikeLine;
        line.setAlpha(145);
        painter.setPen(QPen(line, 1));
        painter.drawLine(QPointF(strikeLeft, geometry.strikeLineY), QPointF(right, geometry.strikeLineY));
    }

    painter.setPen(Qt::NoPen);
    // 将单位径向渐变缩放成光效椭圆，裁剪仍限制在落音区域内。
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
        // 简谱八度点最多显示三个，高八度置于文字上方，低八度置于下方。
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

// 白键先于黑键铺排，随后绘制八度和鼓道标签，形成可缓存的固定底图。
void FallingNotesRenderer::drawKeyboardBase(QPainter& painter, const PlaybackSceneGeometry& geometry,
                                            const PlaybackSceneState& state)
{
    painter.fillRect(geometry.keyboardRect, m_theme.keyboardBackground);

    painter.setPen(QPen(m_theme.whiteKeyBorder, 1));
    painter.setBrush(m_theme.whiteKey);
    // 左侧扩展键只补齐视觉背景，不对应可激活的 MIDI 音高。
    for (const auto& slot : geometry.leftKeyboardExtensionPitches) {
        if (!slot.blackKey)
            painter.drawRect(slot.keyRect.adjusted(0.0, 0.0, -0.5, -0.5));
    }
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

// 用每键代表音符的材质和光强叠加高亮，再恢复遮挡关系及被覆盖的标签。
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

    // 白键矩形延伸到相邻黑键下方；先批量恢复全部黑键前景，再绘活动黑键。
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
    // 活动键填色覆盖了静态底图中的八度标签，此处按相同布局补绘。
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

// 显示当前标记和歌词；错误、加载或空场景时追加状态遮罩。
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

    // 正常谱面无需状态遮罩；其余状态中错误信息优先于加载和空场景提示。
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
