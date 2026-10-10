// 倍率控件实现：协调悬停、编辑焦点与窗口激活，避免影响原生 Vulkan 窗口输入。
#include "playbackratecontrol.h"

#include "presentation/theme/widgetstyles.h"

#include <QApplication>
#include <QCursor>
#include <QEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QScreen>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>

#include <algorithm>

namespace midi_play::presentation {

// 创建工具浮层及双输入控件，用户编辑统一经过规范化和单次通知。
PlaybackRateControl::PlaybackRateControl(QWidget* parent)
    : QToolButton(parent)
{
    setObjectName(QStringLiteral("playbackRateButton"));
    setToolButtonStyle(Qt::ToolButtonTextOnly);
    setFixedSize(72, 44);
    setFocusPolicy(Qt::StrongFocus);
    setAccessibleName(QStringLiteral("播放速度"));

    // Qt::Popup 会抓取鼠标并干扰按钮及原生 Vulkan 容器；工具窗口支持悬停与文本输入。
    m_panel = new QFrame(this, Qt::Tool | Qt::FramelessWindowHint);
    m_panel->setObjectName(QStringLiteral("playbackRatePopup"));
    m_panel->setAttribute(Qt::WA_ShowWithoutActivating);
    auto* layout = new QHBoxLayout(m_panel);
    layout->setContentsMargins(12, 9, 12, 9);
    layout->setSpacing(8);
    auto* label = new QLabel(QStringLiteral("速度"), m_panel);
    label->setObjectName(QStringLiteral("playbackRateLabel"));
    m_slider = new QSlider(Qt::Horizontal, m_panel);
    m_slider->setObjectName(QStringLiteral("playbackRateSlider"));
    m_slider->setRange(midi_play::settings::kMinimumPlaybackRatePercent,
                       midi_play::settings::kMaximumPlaybackRatePercent);
    m_slider->setSingleStep(1);
    m_slider->setPageStep(10);
    m_slider->setFixedWidth(172);
    m_slider->setAccessibleName(QStringLiteral("播放速度滑块"));
    m_slider->setFocusPolicy(Qt::ClickFocus);
    m_editor = new QSpinBox(m_panel);
    m_editor->setObjectName(QStringLiteral("playbackRateSpinBox"));
    m_editor->setRange(midi_play::settings::kMinimumPlaybackRatePercent,
                       midi_play::settings::kMaximumPlaybackRatePercent);
    m_editor->setSuffix(QStringLiteral("%"));
    m_editor->setFixedWidth(80);
    // 禁止逐字符提交：完整文本在回车或失焦时应用，滑块和步进按钮仍实时生效。
    m_editor->setKeyboardTracking(false);
    m_editor->setAccessibleName(QStringLiteral("播放速度百分比"));
    label->setBuddy(m_editor);
    layout->addWidget(label);
    layout->addWidget(m_slider);
    layout->addWidget(m_editor);

    m_hideTimer.setSingleShot(true);
    m_hideTimer.setInterval(220);
    connect(&m_hideTimer, &QTimer::timeout, this, &PlaybackRateControl::hidePanelIfInactive);
    connect(this, &QToolButton::clicked, this, [this] { showPanel(true); });
    connect(m_slider, &QSlider::valueChanged, this, &PlaybackRateControl::applyUserRate);
    connect(m_editor, qOverload<int>(&QSpinBox::valueChanged),
            this, &PlaybackRateControl::applyUserRate);
    connect(qApp, &QGuiApplication::applicationStateChanged, this,
            [this](Qt::ApplicationState state) {
                // 原生激活通知可能滞后于面板开关；延迟后重新检查应用的实际状态。
                if (state != Qt::ApplicationActive && m_panel->isVisible()) m_hideTimer.start();
            });

    setThemeMode(midi_play::settings::kDefaultThemeMode);
    setRatePercent(m_ratePercent);
}

PlaybackRateControl::~PlaybackRateControl()
{
    // 在 QWidget 销毁子面板前移除全局过滤器。
    qApp->removeEventFilter(this);
}

void PlaybackRateControl::setThemeMode(midi_play::settings::ThemeMode mode)
{
    const auto& current = theme::themeFor(mode);
    const auto palette = theme::widgetPalette(current);
    const auto style = theme::playbackRateStyle(current);
    setPalette(palette);
    setStyleSheet(style);
    m_panel->setPalette(palette);
    m_panel->setStyleSheet(style);
}

// 回填实际倍率并同步滑块、编辑器及按钮；阻断输入信号避免形成提交循环。
void PlaybackRateControl::setRatePercent(int percent)
{
    m_ratePercent = midi_play::settings::normalizePlaybackRatePercent(percent);
    const QSignalBlocker sliderBlocker(m_slider);
    const QSignalBlocker editorBlocker(m_editor);
    m_slider->setValue(m_ratePercent);
    m_editor->setValue(m_ratePercent);
    setText(QStringLiteral("%1%").arg(m_ratePercent));
    setToolTip(QStringLiteral("播放速度 %1%（20%～200%）").arg(m_ratePercent));
}

// 用户编辑先统一显示，再发布候选倍率；应用服务可回填拒绝后的实际值。
void PlaybackRateControl::applyUserRate(int percent)
{
    const int normalized = midi_play::settings::normalizePlaybackRatePercent(percent);
    if (normalized == m_ratePercent) return;
    setRatePercent(normalized);
    emit ratePercentEdited(normalized);
}

void PlaybackRateControl::enterEvent(QEnterEvent* event)
{
    QToolButton::enterEvent(event);
    showPanel();
}

void PlaybackRateControl::leaveEvent(QEvent* event)
{
    QToolButton::leaveEvent(event);
    m_hideTimer.start();
}

// 在当前屏幕可用区域定位浮层；悬停展示不抢焦点，显式点击可激活编辑器。
void PlaybackRateControl::showPanel(bool focusEditor)
{
    if (!isVisible() || !isEnabled()) return;
    m_hideTimer.stop();
    if (!m_panel->isVisible()) {
        m_panel->ensurePolished();
        const QSize panelSize = m_panel->sizeHint();
        const QRect buttonRect(mapToGlobal(QPoint()), size());
        const QScreen* targetScreen = QGuiApplication::screenAt(buttonRect.center());
        if (!targetScreen) targetScreen = screen();
        // 优先放在按钮上方，空间不足时改到下方，再夹取到屏幕可用边界。
        const QRect available = targetScreen->availableGeometry().adjusted(4, 4, -4, -4);
        QPoint position(buttonRect.center().x() - panelSize.width() / 2,
                        buttonRect.top() - panelSize.height() - 6);
        if (position.y() < available.top()) position.setY(buttonRect.bottom() + 7);
        position.setX(std::clamp(position.x(), available.left(),
                                std::max(available.left(), available.right() - panelSize.width() + 1)));
        position.setY(std::clamp(position.y(), available.top(),
                                std::max(available.top(), available.bottom() - panelSize.height() + 1)));
        m_panel->setGeometry(QRect(position, panelSize));
        // 面板可见期间过滤外部点击，包含 Vulkan QWindow 的鼠标事件。
        qApp->installEventFilter(this);
        m_panel->show();
    }
    if (focusEditor) {
        m_panel->activateWindow();
        m_editor->setFocus(Qt::OtherFocusReason);
        m_editor->selectAll();
    }
}

bool PlaybackRateControl::containsPointer() const
{
    const QPoint position = QCursor::pos();
    return QRect(mapToGlobal(QPoint()), size()).contains(position)
        || m_panel->geometry().contains(position);
}

// 延迟检查鼠标、拖动和编辑焦点，保证按钮到浮层的移动过程不会提前关闭。
void PlaybackRateControl::hidePanelIfInactive()
{
    if (QGuiApplication::applicationState() != Qt::ApplicationActive) {
        hidePanel();
        return;
    }
    if (containsPointer()) return;
    // 拖动或按键尚未释放时延后检查，文本编辑器有焦点时持续保留面板。
    if (m_slider->isSliderDown() || QApplication::mouseButtons() != Qt::NoButton) {
        m_hideTimer.start();
        return;
    }
    const auto* focus = QApplication::focusWidget();
    if (focus && (focus == m_editor || m_editor->isAncestorOf(focus))) return;
    hidePanel();
}

// 关闭前可提交未完成文本；Esc 路径只撤销尚未提交的编辑内容。
void PlaybackRateControl::hidePanel(bool commitEditor)
{
    m_hideTimer.stop();
    if (!m_panel->isVisible()) return;
    if (commitEditor) m_editor->interpretText();
    else m_editor->setValue(m_ratePercent);
    qApp->removeEventFilter(this);
    m_panel->hide();
}

// 浮层可见时观察全局输入，仅消费本控件范围内的确认/取消按键。
bool PlaybackRateControl::eventFilter(QObject* watched, QEvent* event)
{
    // 主窗口布局或状态变化后关闭面板，避免独立工具窗口残留在旧位置。
    if (watched == window()
        && (event->type() == QEvent::Move || event->type() == QEvent::Resize
            || event->type() == QEvent::Hide || event->type() == QEvent::WindowStateChange)) {
        hidePanel();
    } else if (watched == m_panel && event->type() == QEvent::Hide) {
        m_hideTimer.stop();
        qApp->removeEventFilter(this);
    } else if (watched == m_panel && event->type() == QEvent::WindowDeactivate) {
        m_hideTimer.start();
    } else if (watched == m_panel && event->type() == QEvent::Enter) {
        m_hideTimer.stop();
    } else if ((watched == m_panel && event->type() == QEvent::Leave)
               || event->type() == QEvent::FocusOut
               || event->type() == QEvent::MouseButtonRelease) {
        m_hideTimer.start();
    } else if (event->type() == QEvent::MouseButtonPress) {
        const QPoint position = static_cast<QMouseEvent*>(event)->globalPosition().toPoint();
        if (!m_panel->geometry().contains(position)
            && !QRect(mapToGlobal(QPoint()), size()).contains(position)) {
            hidePanel();
        }
    } else if (event->type() == QEvent::KeyPress) {
        // 只接管按钮及面板内的按键；Esc 撤销文本，Enter 提交并归还主窗口焦点。
        const auto* target = qobject_cast<QWidget*>(watched);
        if (target != this && target != m_panel
            && (!target || !m_panel->isAncestorOf(target))) {
            return QToolButton::eventFilter(watched, event);
        }
        const int key = static_cast<QKeyEvent*>(event)->key();
        if (key == Qt::Key_Escape || key == Qt::Key_Return || key == Qt::Key_Enter) {
            hidePanel(key != Qt::Key_Escape);
            window()->activateWindow();
            setFocus(Qt::OtherFocusReason);
            return true;
        }
    }
    return QToolButton::eventFilter(watched, event);
}

} // namespace midi_play::presentation
