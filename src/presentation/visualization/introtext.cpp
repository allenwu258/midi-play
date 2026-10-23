#include "introtext.h"

#include <QAbstractTextDocumentLayout>
#include <QFontMetricsF>
#include <QPainter>
#include <QTextBoundaryFinder>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextOption>
#include <cmath>
#include <algorithm>

namespace midi_play::presentation::visualization {
namespace {
constexpr qreal padding = 8.0;

QString normalizedText(QString text)
{
    text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    text.replace(QLatin1Char('\r'), QLatin1Char('\n'));
    return text;
}

QString validate(const app::IntroTextConfig& config)
{
    if (!config.enabled) return {};
    if (config.text.trimmed().isEmpty()) return QStringLiteral("请输入入场文字，或关闭此选项。");
    // Bound pathological combining sequences before asking the Unicode boundary iterator.
    if (config.text.size() > 16'000) return QStringLiteral("入场文字过长，请精简内容（最多 2000 字符）。");
    QTextBoundaryFinder finder(QTextBoundaryFinder::Grapheme, config.text);
    int characters = 0;
    while (finder.toNextBoundary() != -1)
        if (++characters > 2'000) return QStringLiteral("入场文字最多支持 2000 个字符。");
    if (normalizedText(config.text).count(QLatin1Char('\n')) >= 50)
        return QStringLiteral("入场文字最多支持 50 行。");
    if (config.fontPixelSize < 12 || config.fontPixelSize > 160)
        return QStringLiteral("字号需要在 12–160 基准 px 之间。");
    if (config.widthPercent < 20 || config.widthPercent > 90)
        return QStringLiteral("文字块宽度需要在 20%–90% 之间。");
    if (config.displayMilliseconds < 0 || config.displayMilliseconds > 600'000)
        return QStringLiteral("完整显示时长需要在 0–600 秒之间。");
    if (config.exitMilliseconds < 100 || config.exitMilliseconds > 5'000)
        return QStringLiteral("退场用时需要在 0.1–5 秒之间。");
    if (config.alignment != app::IntroTextAlignment::Left
        && config.alignment != app::IntroTextAlignment::Center
        && config.alignment != app::IntroTextAlignment::Right)
        return QStringLiteral("文字对齐方式无效。");
    if (config.position != app::IntroTextPosition::Center
        && config.position != app::IntroTextPosition::LowerLeft
        && config.position != app::IntroTextPosition::Custom)
        return QStringLiteral("文字块位置无效。");
    if (config.exitEffect != app::IntroTextExitEffect::Fade
        && config.exitEffect != app::IntroTextExitEffect::SlideDown)
        return QStringLiteral("退场效果无效。");
    return {};
}

QSizeF layoutDocument(QTextDocument& document, const app::IntroTextConfig& config)
{
    QFont font(config.fontFamily);
    font.setPixelSize(config.fontPixelSize);
    font.setHintingPreference(QFont::PreferNoHinting);
    document.setDefaultFont(font);
    document.setDocumentMargin(padding);
    document.setUndoRedoEnabled(false);
    QTextOption option;
    option.setUseDesignMetrics(true);
    option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    option.setAlignment((config.alignment == app::IntroTextAlignment::Left ? Qt::AlignLeft
        : config.alignment == app::IntroTextAlignment::Right ? Qt::AlignRight : Qt::AlignHCenter)
        | Qt::AlignAbsolute);
    option.setTabStopDistance(QFontMetricsF(font).horizontalAdvance(QLatin1Char(' ')) * 4);
    document.setDefaultTextOption(option);
    document.setPlainText(normalizedText(config.text));
    document.setTextWidth(1280.0 * config.widthPercent / 100.0);
    QTextCursor cursor(&document);
    cursor.select(QTextCursor::Document);
    QTextCharFormat format;
    format.setForeground(Qt::white);
    format.setTextOutline(QPen(QColor(0, 0, 0, 220), 2.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    cursor.mergeCharFormat(format);
    return document.documentLayout()->documentSize();
}

QString boundsError(const QRectF& bounds)
{
    if (bounds.left() < -0.001 || bounds.top() < -0.001
        || bounds.right() > 1280.001 || bounds.bottom() > 720.001)
        return QStringLiteral("入场文字超出画布，请调整内容、字号、宽度或位置。");
    return {};
}

bool sameLayout(const app::IntroTextConfig& a, const app::IntroTextConfig& b)
{
    return a.text == b.text && a.fontFamily == b.fontFamily
        && a.fontPixelSize == b.fontPixelSize && a.alignment == b.alignment
        && a.widthPercent == b.widthPercent;
}
} // namespace

QRectF IntroTextAsset::blockBounds(const app::IntroTextConfig& config, QSizeF size)
{
    QPointF topLeft((1280.0 - size.width()) / 2.0, (720.0 - size.height()) / 2.0);
    if (config.position == app::IntroTextPosition::LowerLeft)
        topLeft = {64.0, 684.0 - size.height()};
    else if (config.position == app::IntroTextPosition::Custom)
        topLeft += QPointF(config.offsetX, config.offsetY);
    return {topLeft, size};
}

QString IntroTextAsset::validationError(const app::IntroTextConfig& config)
{
    const QString error = validate(config);
    if (!error.isEmpty() || !config.enabled) return error;
    QTextDocument document;
    return boundsError(blockBounds(config, layoutDocument(document, config)));
}

bool IntroTextAsset::prepare(const app::IntroTextConfig& config, QSize outputSize, QString* error)
{
    m_valid = false;
    if (error) error->clear();
    const auto fail = [error](const QString& message) {
        if (error) *error = message;
        return false;
    };
    if (!config.enabled) {
        m_image = {};
        m_bounds = {};
        m_config = config;
        return true;
    }
    const QString validation = validate(config);
    if (!validation.isEmpty()) return fail(validation);
    if (outputSize.width() <= 0 || outputSize.height() <= 0
        || outputSize.width() > 3840 || outputSize.height() > 2160
        || qint64(outputSize.width()) * 9 != qint64(outputSize.height()) * 16)
        return fail(QStringLiteral("入场文字需要有效的 16:9 输出画布（最高 4K）。"));

    const bool reuseImage = !m_image.isNull() && outputSize == m_outputSize && sameLayout(config, m_config);
    QTextDocument document;
    const QSizeF size = reuseImage ? m_bounds.size() : layoutDocument(document, config);
    const QRectF bounds = blockBounds(config, size);
    const QString geometryError = boundsError(bounds);
    if (!geometryError.isEmpty()) return fail(geometryError);

    if (!reuseImage) {
        const qreal scale = outputSize.width() / 1280.0;
        QImage image(QSize(qCeil(size.width() * scale), qCeil(size.height() * scale)),
                     QImage::Format_ARGB32_Premultiplied);
        if (image.isNull()) return fail(QStringLiteral("无法分配入场文字图像资源。"));
        image.fill(Qt::transparent);
        QPainter painter(&image);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setRenderHint(QPainter::TextAntialiasing);
        painter.scale(scale, scale);
        QAbstractTextDocumentLayout::PaintContext context;
        context.clip = QRectF(QPointF(), size);
        document.documentLayout()->draw(&painter, context);
        painter.end();
        m_image = std::move(image);
    }
    m_config = config;
    m_bounds = bounds;
    m_outputSize = outputSize;
    m_valid = true;
    return true;
}

QRectF IntroTextAsset::outputBounds() const
{
    const qreal scale = m_outputSize.width() / 1280.0;
    return {m_bounds.topLeft() * scale, m_bounds.size() * scale};
}

IntroTextAnimationState IntroTextAsset::animationState(qint64 outputTimeUs) const
{
    if (!m_valid || !m_config.enabled) return {};
    const qint64 elapsed = std::max<qint64>(0, outputTimeUs);
    const qint64 hold = qint64(m_config.displayMilliseconds) * 1000;
    const qint64 duration = qint64(m_config.exitMilliseconds) * 1000;
    if (elapsed >= hold + duration) return {};
    const qreal u = std::clamp(qreal(elapsed - hold) / duration, 0.0, 1.0);
    const qreal travel = m_outputSize.height() - outputBounds().top() + 4 * m_outputSize.width() / 1280.0;
    return {1.0 - u * u * (3.0 - 2.0 * u),
            m_config.exitEffect == app::IntroTextExitEffect::SlideDown ? travel * u * u : 0.0};
}

void IntroTextAsset::paint(QPainter& painter, qint64 outputTimeUs, bool forceVisible) const
{
    if (!m_valid) return;
    const auto state = forceVisible ? IntroTextAnimationState{1.0, 0.0} : animationState(outputTimeUs);
    if (state.opacity <= 0) return;
    painter.save();
    painter.setClipRect(QRectF(QPointF(), m_outputSize), Qt::IntersectClip);
    painter.setOpacity(painter.opacity() * state.opacity);
    painter.drawImage(outputBounds().topLeft() + QPointF(0, state.offsetY), m_image);
    painter.restore();
}

bool IntroTextAsset::composite(QImage& frame, qint64 outputTimeUs) const
{
    if (animationState(outputTimeUs).opacity <= 0) return true;
    if (frame.size() != m_outputSize || frame.isNull()) return false;
    QPainter painter(&frame);
    if (!painter.isActive()) return false;
    paint(painter, outputTimeUs);
    return true;
}
} // namespace midi_play::presentation::visualization
