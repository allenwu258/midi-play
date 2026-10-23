#include "introtext.h"

#include <QAbstractTextDocumentLayout>
#include <QFontMetricsF>
#include <QPainter>
#include <QPen>
#include <QTextBoundaryFinder>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextOption>
#include <algorithm>
#include <cmath>

namespace midi_play::presentation::visualization {
namespace {
constexpr qreal horizontalPadding = 8.0;
constexpr qreal verticalPadding = 8.0;
constexpr qreal titleBodySpacing = 12.0;
constexpr int minimumFontSize = 12;
constexpr int maximumFontSize = 160;
constexpr int maximumOutlineWidthTenths = 40;

QString normalizedText(QString text)
{
    text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    text.replace(QLatin1Char('\r'), QLatin1Char('\n'));
    return text;
}

int graphemeCount(const QString& text)
{
    QTextBoundaryFinder finder(QTextBoundaryFinder::Grapheme, text);
    int count = 0;
    while (finder.toNextBoundary() != -1) ++count;
    return count;
}

QString validate(const app::IntroTextConfig& config)
{
    if (!config.enabled) return {};
    const QString title = normalizedText(config.titleText);
    const QString body = normalizedText(config.bodyText);
    if (title.trimmed().isEmpty() && body.trimmed().isEmpty())
        return QStringLiteral("请输入标题或正文，或关闭此选项。");

    // Bound pathological combining sequences before asking the Unicode boundary iterator.
    if (title.size() + body.size() > 16'000)
        return QStringLiteral("入场文字过长，请精简内容（最多 2000 字符）。");
    if (graphemeCount(title) + graphemeCount(body) > 2'000)
        return QStringLiteral("标题和正文合计最多支持 2000 个字符。");
    if (title.count(QLatin1Char('\n')) + body.count(QLatin1Char('\n')) >= 50)
        return QStringLiteral("标题和正文合计最多支持 50 行。");
    if (config.titleFontPixelSize < minimumFontSize || config.titleFontPixelSize > maximumFontSize
        || config.bodyFontPixelSize < minimumFontSize || config.bodyFontPixelSize > maximumFontSize)
        return QStringLiteral("标题和正文字号需要在 12–160 基准 px 之间。");
    if (config.outlineWidthTenths < 0 || config.outlineWidthTenths > maximumOutlineWidthTenths)
        return QStringLiteral("描边粗细需要在 0–4.0 基准 px 之间。");
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

void layoutDocument(QTextDocument& document, const QString& text,
                    const app::IntroTextConfig& config, int fontPixelSize, qreal textWidth)
{
    QFont font(config.fontFamily);
    font.setPixelSize(fontPixelSize);
    font.setHintingPreference(QFont::PreferNoHinting);
    document.setDefaultFont(font);
    document.setDocumentMargin(0.0);
    document.setUndoRedoEnabled(false);
    QTextOption option;
    option.setUseDesignMetrics(true);
    option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    option.setAlignment((config.alignment == app::IntroTextAlignment::Left ? Qt::AlignLeft
        : config.alignment == app::IntroTextAlignment::Right ? Qt::AlignRight : Qt::AlignHCenter)
        | Qt::AlignAbsolute);
    option.setTabStopDistance(QFontMetricsF(font).horizontalAdvance(QLatin1Char(' ')) * 4);
    document.setDefaultTextOption(option);
    document.setPlainText(text);
    document.setTextWidth(textWidth);

    QTextCursor cursor(&document);
    cursor.select(QTextCursor::Document);
    QTextCharFormat format;
    format.setForeground(Qt::white);
    if (config.outlineWidthTenths > 0) {
        QPen outline(QColor(0, 0, 0, 220));
        outline.setWidthF(config.outlineWidthTenths / 10.0);
        outline.setStyle(Qt::SolidLine);
        outline.setCapStyle(Qt::RoundCap);
        outline.setJoinStyle(Qt::RoundJoin);
        format.setTextOutline(outline);
    }
    cursor.mergeCharFormat(format);
}

struct TextLayout {
    QTextDocument title;
    QTextDocument body;
    QSizeF size;
    qreal titleHeight = 0.0;
    qreal bodyHeight = 0.0;
    bool hasTitle = false;
    bool hasBody = false;
};

void layoutText(const app::IntroTextConfig& config, TextLayout& result)
{
    const QString title = normalizedText(config.titleText);
    const QString body = normalizedText(config.bodyText);
    result.hasTitle = !title.trimmed().isEmpty();
    result.hasBody = !body.trimmed().isEmpty();
    const qreal width = 1280.0 * config.widthPercent / 100.0;
    const qreal textWidth = std::max<qreal>(1.0, width - horizontalPadding * 2.0);
    if (result.hasTitle) {
        layoutDocument(result.title, title, config, config.titleFontPixelSize, textWidth);
        result.titleHeight = result.title.documentLayout()->documentSize().height();
    }
    if (result.hasBody) {
        layoutDocument(result.body, body, config, config.bodyFontPixelSize, textWidth);
        result.bodyHeight = result.body.documentLayout()->documentSize().height();
    }
    const qreal gap = result.hasTitle && result.hasBody ? titleBodySpacing : 0.0;
    result.size = QSizeF(width, verticalPadding * 2.0 + result.titleHeight
        + gap + result.bodyHeight);
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
    return a.titleText == b.titleText && a.bodyText == b.bodyText
        && a.fontFamily == b.fontFamily && a.titleFontPixelSize == b.titleFontPixelSize
        && a.bodyFontPixelSize == b.bodyFontPixelSize
        && a.outlineWidthTenths == b.outlineWidthTenths
        && a.alignment == b.alignment && a.widthPercent == b.widthPercent;
}

void drawDocument(QTextDocument& document, QPainter& painter, const QPointF& position, qreal height)
{
    painter.save();
    painter.translate(position);
    QAbstractTextDocumentLayout::PaintContext context;
    context.clip = QRectF(0.0, 0.0, document.textWidth(), height);
    document.documentLayout()->draw(&painter, context);
    painter.restore();
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
    TextLayout layout;
    layoutText(config, layout);
    return boundsError(blockBounds(config, layout.size));
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
    TextLayout layout;
    if (!reuseImage) layoutText(config, layout);
    const QSizeF size = reuseImage ? m_bounds.size() : layout.size;
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
        qreal y = verticalPadding;
        if (layout.hasTitle) {
            drawDocument(layout.title, painter, {horizontalPadding, y}, layout.titleHeight);
            y += layout.titleHeight;
        }
        if (layout.hasBody) {
            if (layout.hasTitle) y += titleBodySpacing;
            drawDocument(layout.body, painter, {horizontalPadding, y}, layout.bodyHeight);
        }
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
