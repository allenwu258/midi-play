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

PlaybackRateControl::PlaybackRateControl(QWidget* parent)
    : QToolButton(parent)
{
    setObjectName(QStringLiteral("playbackRateButton"));
    setToolButtonStyle(Qt::ToolButtonTextOnly);
    setFixedSize(72, 44);
    setFocusPolicy(Qt::StrongFocus);
    setAccessibleName(QStringLiteral("播放速度"));

    // A Qt::Popup grabs the mouse when opened by hover, breaking delivery to
    // the trigger and the native Vulkan container. A transient tool window
    // allows both hover and text input without a modal mouse grab.
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
    // Typing 125 must not momentarily apply 20, 12 or 25 percent. Commit a
    // complete edit on Enter/focus loss; slider/step buttons still apply live.
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
                // Native activation notifications may arrive after a tool
                // window has already closed/reopened. Recheck the current
                // state after the transition settles instead of hiding now.
                if (state != Qt::ApplicationActive && m_panel->isVisible()) m_hideTimer.start();
            });

    setThemeMode(midi_play::settings::kDefaultThemeMode);
    setRatePercent(m_ratePercent);
}

PlaybackRateControl::~PlaybackRateControl()
{
    // Remove the application filter before QWidget destroys the child panel.
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
        const QRect available = targetScreen->availableGeometry().adjusted(4, 4, -4, -4);
        QPoint position(buttonRect.center().x() - panelSize.width() / 2,
                        buttonRect.top() - panelSize.height() - 6);
        if (position.y() < available.top()) position.setY(buttonRect.bottom() + 7);
        position.setX(std::clamp(position.x(), available.left(),
                                std::max(available.left(), available.right() - panelSize.width() + 1)));
        position.setY(std::clamp(position.y(), available.top(),
                                std::max(available.top(), available.bottom() - panelSize.height() + 1)));
        m_panel->setGeometry(QRect(position, panelSize));
        // Filter outside clicks (including QWindow events from Vulkan) only
        // while visible, so normal playback pays no global event-filter cost.
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

void PlaybackRateControl::hidePanelIfInactive()
{
    if (QGuiApplication::applicationState() != Qt::ApplicationActive) {
        hidePanel();
        return;
    }
    if (containsPointer()) return;
    if (m_slider->isSliderDown() || QApplication::mouseButtons() != Qt::NoButton) {
        m_hideTimer.start();
        return;
    }
    const auto* focus = QApplication::focusWidget();
    if (focus && (focus == m_editor || m_editor->isAncestorOf(focus))) return;
    hidePanel();
}

void PlaybackRateControl::hidePanel(bool commitEditor)
{
    m_hideTimer.stop();
    if (!m_panel->isVisible()) return;
    if (commitEditor) m_editor->interpretText();
    else m_editor->setValue(m_ratePercent);
    qApp->removeEventFilter(this);
    m_panel->hide();
}

bool PlaybackRateControl::eventFilter(QObject* watched, QEvent* event)
{
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
