// 标题栏按钮实现：绘制物理像素对齐的系统符号，协调主题、激活和原生输入状态。
#include "captionbuttons.h"

#include "presentation/theme/apptheme.h"

#include <QEvent>
#include <QHBoxLayout>
#include <QPainter>
#include <QToolButton>

#include <algorithm>

namespace midi_play::presentation::windowchrome {
namespace {

// 单个系统按钮的外观状态；保留 QToolButton 的键盘与无障碍激活行为。
class CaptionButton final : public QToolButton {
public:
    CaptionButton(CaptionCommand command, QWidget* parent) : QToolButton(parent), command(command)
    {
        setAutoRaise(true);
        setFocusPolicy(Qt::StrongFocus);
        setMouseTracking(true);
    }

    CaptionCommand command;
    midi_play::settings::ThemeMode theme = midi_play::settings::kDefaultThemeMode;
    bool restoredGlyph = false;
    bool active = true;
    bool nativeHovered = false; // 非客户区输入不会产生普通 Qt 悬停事件，需额外同步。
    bool highContrast = false;
    QColor systemText, systemBackground, systemHighlight, systemHighlightedText;

protected:
    // Qt 客户区悬停变化时重新绘制，非客户区悬停由控制器另行同步。
    bool event(QEvent* event) override
    {
        if (event->type() == QEvent::Enter || event->type() == QEvent::Leave) update();
        return QToolButton::event(event);
    }

    // 统一绘制背景、命令符号和焦点框；不依赖平台样式的默认按钮图形。
    void paintEvent(QPaintEvent*) override
    {
        const auto& colors = theme::themeFor(theme).widgets;
        const bool hovered = nativeHovered || underMouse();
        QColor foreground = highContrast ? systemText : (active ? colors.text : colors.mutedText);
        QColor background = highContrast ? systemBackground : colors.panel;
        // 高对比度使用系统选中色；普通主题中关闭按钮具有独立警示色。
        if (!isEnabled()) foreground = highContrast ? systemText : colors.disabledText;
        else if (hovered || isDown()) {
            if (highContrast) {
                foreground = systemHighlightedText;
                background = systemHighlight;
            } else if (command == CaptionCommand::Close) {
                foreground = Qt::white;
                background = isDown() ? colors.closePressed : colors.closeHover;
            } else {
                background = isDown() ? colors.pressed : colors.hover;
            }
        }

        QPainter painter(this);
        painter.fillRect(rect(), background);
        // 换算到物理像素绘制，使非整数 DPI 下的细线仍与像素网格对齐。
        const qreal dpr = devicePixelRatioF();
        painter.scale(1.0 / dpr, 1.0 / dpr);
        const int extent = std::max(8, qRound(10 * dpr));
        const int stroke = std::max(1, qRound(dpr));
        const int x = qRound((width() * dpr - extent) / 2.0);
        const int y = qRound((height() * dpr - extent) / 2.0);
        painter.setPen(QPen(foreground, stroke, Qt::SolidLine, Qt::SquareCap));
        if (command == CaptionCommand::Minimize) {
            painter.drawLine(x, y + extent / 2, x + extent, y + extent / 2);
        } else if (command == CaptionCommand::Close) {
            painter.drawLine(x, y, x + extent, y + extent);
            painter.drawLine(x + extent, y, x, y + extent);
        } else if (restoredGlyph) {
            const int offset = std::max(2, qRound(2 * dpr));
            painter.drawRect(QRect(x + offset, y, extent - offset, extent - offset));
            painter.fillRect(QRect(x, y + offset, extent - offset, extent - offset), background);
            painter.drawRect(QRect(x, y + offset, extent - offset, extent - offset));
        } else {
            painter.drawRect(QRect(x, y, extent, extent));
        }
        // 键盘焦点保留独立虚线框，避免只能依赖鼠标悬停识别当前操作。
        if (hasFocus()) {
            painter.setPen(QPen(foreground, 1, Qt::DotLine));
            painter.drawRect(QRect(3, 3, qRound(width() * dpr) - 7, qRound(height() * dpr) - 7));
        }
    }
};

} // namespace

// 固定命令枚举与按钮数组下标一一对应，点击统一转换为领域无关的窗口命令。
CaptionButtons::CaptionButtons(QWidget* parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("windowCaptionButtons"));
    auto* row = new QHBoxLayout(this);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(0);
    const std::array<QString, 3> names {QStringLiteral("最小化窗口"),
        QStringLiteral("最大化窗口"), QStringLiteral("关闭窗口")};
    const std::array<QString, 3> ids {QStringLiteral("windowMinimizeButton"),
        QStringLiteral("windowMaximizeButton"), QStringLiteral("windowCloseButton")};
    for (int i = 0; i < 3; ++i) {
        const auto command = static_cast<CaptionCommand>(i);
        auto* button = new CaptionButton(command, this);
        m_buttons[i] = button;
        button->setObjectName(ids[i]);
        button->setAccessibleName(names[i]);
        button->setToolTip(names[i]);
        row->addWidget(button);
        connect(button, &QToolButton::clicked, this, [this, command] { emit commandRequested(command); });
    }
    setMetrics(138, 30);
}

QToolButton* CaptionButtons::button(CaptionCommand command) const noexcept
{
    return m_buttons[static_cast<size_t>(command)];
}

// 接收逻辑像素度量，余数分配给前几个按钮，保持三按钮总宽精确一致。
void CaptionButtons::setMetrics(int totalWidth, int height)
{
    totalWidth = std::max(90, totalWidth);
    height = std::max(28, height);
    setFixedSize(totalWidth, height);
    for (int i = 0; i < 3; ++i)
        m_buttons[i]->setFixedSize(totalWidth / 3 + (i < totalWidth % 3 ? 1 : 0), height);
}

void CaptionButtons::setTheme(midi_play::settings::ThemeMode theme)
{
    for (auto* button : m_buttons) {
        static_cast<CaptionButton*>(button)->theme = theme;
        button->update();
    }
}

void CaptionButtons::setMaximized(bool maximized)
{
    auto* maximize = static_cast<CaptionButton*>(button(CaptionCommand::MaximizeOrRestore));
    maximize->restoredGlyph = maximized;
    const QString label = maximized ? QStringLiteral("还原窗口") : QStringLiteral("最大化窗口");
    maximize->setAccessibleName(label);
    maximize->setToolTip(label);
    maximize->update();
}

void CaptionButtons::setWindowActive(bool active)
{
    for (auto* button : m_buttons) {
        static_cast<CaptionButton*>(button)->active = active;
        button->update();
    }
}

// 原生命中结果只允许一个按钮悬停；nullptr 清除全部非客户区悬停状态。
void CaptionButtons::setNativeHovered(QToolButton* hovered)
{
    for (auto* button : m_buttons) {
        static_cast<CaptionButton*>(button)->nativeHovered = button == hovered;
        button->update();
    }
}

// 桥接原生按压外观，不在此处执行命令；释放命中由控制器判定后触发 click。
void CaptionButtons::setNativePressed(QToolButton* pressed)
{
    for (auto* button : m_buttons) button->setDown(button == pressed);
}

// 高对比度启用时使用系统语义色，普通主题仍由各按钮的应用色表决定。
void CaptionButtons::setSystemColors(bool highContrast, const QColor& text, const QColor& background,
                                     const QColor& highlight, const QColor& highlightedText)
{
    for (auto* widget : m_buttons) {
        auto* button = static_cast<CaptionButton*>(widget);
        button->highContrast = highContrast;
        button->systemText = text;
        button->systemBackground = background;
        button->systemHighlight = highlight;
        button->systemHighlightedText = highlightedText;
        button->update();
    }
}

} // namespace midi_play::presentation::windowchrome
