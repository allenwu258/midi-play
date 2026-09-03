#include "mainwindow.h"

#include "app/playerapplicationservice.h"
#include "app/settingsservice.h"
#include "playbackmetadatapresenter.h"
#include "presentation/settings/settingsdialog.h"
#include "presentation/windowchrome/customtitlebar.h"
#include "presentation/visualization/fallingnotesview.h"

#include <QFileDialog>
#include <QFileInfo>
#include <QEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPainter>
#include <QPixmap>
#include <QResizeEvent>
#include <QSlider>
#include <QStyle>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidget>
#include <QtMath>

#if defined(Q_OS_WIN)
#include <windows.h>
#include <windowsx.h>
#endif

#include <algorithm>

namespace midi_play::presentation {
namespace {

QFrame* verticalSeparator(QWidget* parent)
{
    auto* separator = new QFrame(parent);
    separator->setFrameShape(QFrame::VLine);
    separator->setFrameShadow(QFrame::Plain);
    separator->setObjectName(QStringLiteral("toolbarSeparator"));
    return separator;
}

QToolButton* toolButton(QWidget* parent, const QIcon& icon, const QString& text,
                        const QString& tooltip, bool iconOnly = false)
{
    auto* button = new QToolButton(parent);
    button->setIcon(icon);
    button->setText(text);
    button->setToolTip(tooltip);
    button->setToolButtonStyle(iconOnly ? Qt::ToolButtonIconOnly : Qt::ToolButtonTextBesideIcon);
    button->setAutoRaise(true);
    if (iconOnly) button->setFixedSize(38, 38);
    else button->setMinimumHeight(36);
    return button;
}

enum class WindowControlGlyph {
    Minimize,
    Maximize,
    Restore,
    Close,
};

QIcon windowControlIcon(WindowControlGlyph glyph)
{
    constexpr int kIconSize = 18;
    QPixmap pixmap(kIconSize, kIconSize);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, false);
    QPen pen(QColor(QStringLiteral("#dfe1dc")));
    pen.setWidth(2);
    pen.setCapStyle(Qt::SquareCap);
    pen.setJoinStyle(Qt::MiterJoin);
    painter.setPen(pen);

    switch (glyph) {
    case WindowControlGlyph::Minimize:
        painter.drawLine(QPoint(3, 11), QPoint(14, 11));
        break;
    case WindowControlGlyph::Maximize:
        painter.drawRect(QRect(3, 3, 11, 11));
        break;
    case WindowControlGlyph::Restore:
        painter.drawRect(QRect(5, 2, 10, 10));
        painter.drawLine(QPoint(3, 6), QPoint(3, 15));
        painter.drawLine(QPoint(3, 15), QPoint(12, 15));
        painter.drawLine(QPoint(3, 6), QPoint(5, 6));
        break;
    case WindowControlGlyph::Close:
        painter.drawLine(QPoint(4, 4), QPoint(13, 13));
        painter.drawLine(QPoint(13, 4), QPoint(4, 13));
        break;
    }
    return QIcon(pixmap);
}

} // namespace

MainWindow::MainWindow(app::PlayerApplicationService* service,
                       app::SettingsService* settingsService,
                       QWidget* parent)
    : QMainWindow(parent), m_service(service), m_settingsService(settingsService)
{
    setWindowTitle(QStringLiteral("MIDI Play"));
    resize(1180, 760);
    setMinimumSize(720, 540);

    auto* central = new QWidget(this);
    central->setObjectName(QStringLiteral("applicationRoot"));
    auto* root = new QVBoxLayout(central);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    auto* topBar = new windowchrome::CustomTitleBar(central);
    m_topBar = topBar;
    topBar->setObjectName(QStringLiteral("topBar"));
    topBar->setFixedHeight(58);
    auto* topLayout = new QHBoxLayout(topBar);
    topLayout->setContentsMargins(16, 0, 12, 0);
    topLayout->setSpacing(10);

    auto* brand = new QLabel(QStringLiteral("MIDI Play"), topBar);
    brand->setObjectName(QStringLiteral("brandLabel"));
    topLayout->addWidget(brand);
    topLayout->addWidget(verticalSeparator(topBar));
    m_fileLabel = new QLabel(QStringLiteral("未加载音乐文件"), topBar);
    m_fileLabel->setObjectName(QStringLiteral("fileLabel"));
    m_fileLabel->setMinimumWidth(100);
    m_fileLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    topLayout->addWidget(m_fileLabel, 1);

    m_keyLabel = new QLabel(QStringLiteral("--"), topBar);
    m_keyLabel->setObjectName(QStringLiteral("metricLabel"));
    m_keyLabel->setAccessibleName(QStringLiteral("当前调号"));
    m_keyLabel->setToolTip(QStringLiteral("当前调号"));
    m_timeSignatureLabel = new QLabel(QStringLiteral("--/--"), topBar);
    m_timeSignatureLabel->setObjectName(QStringLiteral("metricLabel"));
    m_timeSignatureLabel->setAccessibleName(QStringLiteral("当前拍号"));
    m_timeSignatureLabel->setToolTip(QStringLiteral("当前拍号"));
    m_tempoLabel = new QLabel(QStringLiteral("-- BPM"), topBar);
    m_tempoLabel->setObjectName(QStringLiteral("metricLabel"));
    m_tempoLabel->setAccessibleName(QStringLiteral("当前速度"));
    m_tempoLabel->setToolTip(QStringLiteral("当前速度"));
    topLayout->addWidget(m_keyLabel);
    topLayout->addWidget(m_timeSignatureLabel);
    topLayout->addWidget(m_tempoLabel);
    topLayout->addWidget(verticalSeparator(topBar));

    m_timeLabel = new QLabel(QStringLiteral("00:00 / 00:00"), topBar);
    m_timeLabel->setObjectName(QStringLiteral("timeLabel"));
    m_timeLabel->setAccessibleName(QStringLiteral("播放时间"));
    m_timeLabel->setMinimumWidth(118);
    m_timeLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    topLayout->addWidget(m_timeLabel);
    topLayout->addWidget(verticalSeparator(topBar));

    m_soundFontLabel = new QLabel(QStringLiteral("音源未加载"), topBar);
    m_soundFontLabel->setObjectName(QStringLiteral("soundFontLabel"));
    topLayout->addWidget(m_soundFontLabel);

    auto* openMusic = toolButton(topBar, style()->standardIcon(QStyle::SP_DialogOpenButton),
                                 QStringLiteral("打开乐曲"), QStringLiteral("打开 MusicXML 或 MIDI 文件"));
    auto* settingsButton = toolButton(topBar, style()->standardIcon(QStyle::SP_FileDialogDetailedView),
                                      QStringLiteral("设置"), QStringLiteral("打开播放器设置"));
    topLayout->addWidget(openMusic);
    topLayout->addWidget(settingsButton);

    m_windowControlsSeparator = verticalSeparator(topBar);
    topLayout->addWidget(m_windowControlsSeparator);
    m_minimizeButton = toolButton(topBar, windowControlIcon(WindowControlGlyph::Minimize), {},
                                  QStringLiteral("最小化窗口"), true);
    m_maximizeButton = toolButton(topBar, windowControlIcon(WindowControlGlyph::Maximize), {},
                                  QStringLiteral("最大化窗口"), true);
    m_closeButton = toolButton(topBar, windowControlIcon(WindowControlGlyph::Close), {},
                               QStringLiteral("关闭窗口"), true);
    m_minimizeButton->setObjectName(QStringLiteral("windowMinimizeButton"));
    m_maximizeButton->setObjectName(QStringLiteral("windowMaximizeButton"));
    m_closeButton->setObjectName(QStringLiteral("windowCloseButton"));
    m_minimizeButton->setAccessibleName(QStringLiteral("最小化窗口"));
    m_maximizeButton->setAccessibleName(QStringLiteral("最大化或还原窗口"));
    m_closeButton->setAccessibleName(QStringLiteral("关闭窗口"));
    topLayout->addWidget(m_minimizeButton);
    topLayout->addWidget(m_maximizeButton);
    topLayout->addWidget(m_closeButton);
    m_windowControlsSeparator->setVisible(false);
    m_minimizeButton->setVisible(false);
    m_maximizeButton->setVisible(false);
    m_closeButton->setVisible(false);
    topBar->registerDragWidget(brand);
    topBar->registerDragWidget(m_fileLabel);
    topBar->registerDragWidget(m_keyLabel);
    topBar->registerDragWidget(m_timeSignatureLabel);
    topBar->registerDragWidget(m_tempoLabel);
    topBar->registerDragWidget(m_timeLabel);
    topBar->registerDragWidget(m_soundFontLabel);
    root->addWidget(topBar);

    m_visualization = new visualization::FallingNotesView(central);
    root->addWidget(m_visualization, 1);

    auto* transport = new QWidget(central);
    transport->setObjectName(QStringLiteral("transportBar"));
    transport->setFixedHeight(82);
    auto* transportLayout = new QVBoxLayout(transport);
    transportLayout->setContentsMargins(16, 8, 16, 8);
    transportLayout->setSpacing(4);

    m_positionSlider = new QSlider(Qt::Horizontal, transport);
    m_positionSlider->setObjectName(QStringLiteral("positionSlider"));
    m_positionSlider->setRange(0, kSliderResolution);
    m_positionSlider->setEnabled(false);
    transportLayout->addWidget(m_positionSlider);

    auto* controlRow = new QHBoxLayout();
    controlRow->setContentsMargins(0, 0, 0, 0);
    controlRow->setSpacing(6);
    m_playButton = toolButton(transport, style()->standardIcon(QStyle::SP_MediaPlay), QStringLiteral("播放"),
                              QStringLiteral("播放"), true);
    m_pauseButton = toolButton(transport, style()->standardIcon(QStyle::SP_MediaPause), QStringLiteral("暂停"),
                               QStringLiteral("暂停"), true);
    m_stopButton = toolButton(transport, style()->standardIcon(QStyle::SP_MediaStop), QStringLiteral("停止"),
                              QStringLiteral("停止并回到开头"), true);
    controlRow->addWidget(m_playButton);
    controlRow->addWidget(m_pauseButton);
    controlRow->addWidget(m_stopButton);
    controlRow->addStretch();
    m_statusLabel = new QLabel(QStringLiteral("就绪"), transport);
    m_statusLabel->setObjectName(QStringLiteral("statusLabel"));
    controlRow->addWidget(m_statusLabel);
    transportLayout->addLayout(controlRow);
    root->addWidget(transport);
    setCentralWidget(central);

    setStyleSheet(QStringLiteral(R"(
        QWidget#applicationRoot { background: #121416; color: #f0f1ed; }
        QWidget#topBar, QWidget#transportBar { background: #1b1d20; }
        QWidget#topBar { border-bottom: 1px solid #303337; }
        QWidget#transportBar { border-top: 1px solid #303337; }
        QLabel#brandLabel { color: #f0f1ed; font-size: 17px; font-weight: 600; }
        QLabel#fileLabel { color: #c8cbc7; font-size: 12px; }
        QLabel#soundFontLabel, QLabel#statusLabel { color: #8f9691; font-size: 11px; }
        QLabel#metricLabel { color: #bfc3bf; font-size: 11px; }
        QLabel#timeLabel { color: #e6e7e2; font-family: Consolas, monospace; font-size: 11px; }
        QFrame#toolbarSeparator { color: #3a3d40; max-height: 26px; }
        QToolButton { color: #dfe1dc; border: 1px solid transparent; padding: 6px 8px; }
        QToolButton:hover { background: #292c2f; border-color: #3a3e41; }
        QToolButton:pressed { background: #34383b; }
        QToolButton:disabled { color: #676c68; }
        QToolButton#windowCloseButton:hover { background: #c42b2b; border-color: #c42b2b; }
        QToolButton#windowCloseButton:pressed { background: #a51f1f; border-color: #a51f1f; }
        QSlider::groove:horizontal { height: 4px; background: #393d3f; }
        QSlider::sub-page:horizontal { background: #f4d35e; }
        QSlider::handle:horizontal { width: 14px; margin: -5px 0; border-radius: 7px; background: #f0f1ed; }
        QSlider::handle:horizontal:hover { background: #f4d35e; }
    )"));

    connect(openMusic, &QToolButton::clicked, this, &MainWindow::openMusicFile);
    connect(settingsButton, &QToolButton::clicked, this, &MainWindow::showSettings);
    connect(m_minimizeButton, &QToolButton::clicked, this, &MainWindow::showMinimized);
    connect(m_maximizeButton, &QToolButton::clicked, this, [this] {
        if (isMaximized()) showNormal(); else showMaximized();
    });
    connect(m_closeButton, &QToolButton::clicked, this, &MainWindow::close);
    if (m_settingsService) {
        connect(m_settingsService, &app::SettingsService::titleBarModeChanged,
                this, &MainWindow::applyTitleBarMode);
    }
    connect(m_playButton, &QToolButton::clicked, m_service, &app::PlayerApplicationService::play);
    connect(m_pauseButton, &QToolButton::clicked, m_service, &app::PlayerApplicationService::pause);
    connect(m_stopButton, &QToolButton::clicked, m_service, &app::PlayerApplicationService::stop);
    connect(m_positionSlider, &QSlider::sliderPressed, this, [this] { m_sliderDragging = true; });
    connect(m_positionSlider, &QSlider::sliderMoved, this, [this](int value) {
        if (m_durationUs <= 0) return;
        const qint64 previewUs = static_cast<qint64>(
            static_cast<long double>(value) * m_durationUs / kSliderResolution);
        updateTimeDisplay(previewUs, m_durationUs);
        updateMetadata(previewUs);
    });
    connect(m_positionSlider, &QSlider::sliderReleased, this, [this] {
        if (m_durationUs > 0) {
            const qint64 targetUs = static_cast<qint64>(
                static_cast<long double>(m_positionSlider->value()) * m_durationUs / kSliderResolution);
            m_pendingSeekUs = targetUs;
            m_seekPending = true;
            m_service->seek(targetUs);
        }
        m_sliderDragging = false;
    });

    connect(m_service, &app::PlayerApplicationService::documentLoaded, this,
            [this](const QString& title, qint64 duration) {
                m_fileLabel->setText(title);
                m_fileLabel->setToolTip(m_service->fileName());
                m_durationUs = duration;
                m_displayedPositionSecond = -1;
                m_displayedDurationSecond = -1;
                updateTimeDisplay(m_positionUs, m_durationUs);
                m_seekPending = false;
                m_positionSlider->setEnabled(duration > 0);
                m_statusLabel->setText(QStringLiteral("曲目已加载"));
                updateTransportControls();
            });
    connect(m_service, &app::PlayerApplicationService::visualizationReady, this,
            [this](midi_play::visualization::VisualChartPtr chart) {
                m_chart = chart;
                m_metadataTimeline.setChart(m_chart);
                m_visualization->setChart(std::move(chart));
                updateMetadata(m_positionUs);
            });
    connect(m_service, &app::PlayerApplicationService::positionChanged,
            this, &MainWindow::updatePosition);
    connect(m_service, &app::PlayerApplicationService::playbackStateChanged,
            this, &MainWindow::updatePlaybackState);
    connect(m_service, &app::PlayerApplicationService::soundFontLoaded, this, [this](const QString& path) {
        const QFileInfo file(path);
        m_soundFontLabel->setText(file.fileName().isEmpty() ? QStringLiteral("音源已配置") : file.fileName());
        m_soundFontLabel->setToolTip(path);
        m_statusLabel->setText(QStringLiteral("音源已加载"));
    });
    connect(m_service, &app::PlayerApplicationService::soundFontLoadFailed,
            this, [this](const QString& message) {
                m_statusLabel->setText(message);
            });
    connect(m_service, &app::PlayerApplicationService::busyChanged, this, [this](bool busy) {
        m_visualization->setLoading(busy);
        m_statusLabel->setText(busy ? QStringLiteral("正在分析音乐文件...") : QStringLiteral("就绪"));
    });
    connect(m_service, &app::PlayerApplicationService::errorOccurred, this, [this](const QString& message) {
        m_statusLabel->setText(message);
        m_visualization->setErrorMessage(message);
        QMessageBox::warning(this, QStringLiteral("播放器错误"), message);
    });
    if (m_settingsService) {
        connect(m_settingsService, &app::SettingsService::settingsLoadWarning, this,
                [this](const QString& message) {
                    m_statusLabel->setText(message);
                });
        connect(m_settingsService, &app::SettingsService::settingsSaveFailed, this,
                [this](const QString& message) {
                    m_statusLabel->setText(message);
                });
    }

    updateTransportControls();
    updateResponsiveVisibility();
    applyTitleBarMode(m_settingsService ? m_settingsService->titleBarMode()
                                         : midi_play::settings::TitleBarMode::Native);
}

void MainWindow::resizeEvent(QResizeEvent* event)
{
    QMainWindow::resizeEvent(event);
    updateResponsiveVisibility();
}

void MainWindow::changeEvent(QEvent* event)
{
    QMainWindow::changeEvent(event);
    if (event->type() == QEvent::WindowStateChange) {
        updateWindowControlButtons();
    }
}

bool MainWindow::nativeEvent(const QByteArray& eventType, void* message, qintptr* result)
{
#if defined(Q_OS_WIN)
    if (m_titleBarMode == midi_play::settings::TitleBarMode::Custom
        && (eventType == QByteArrayLiteral("windows_generic_MSG")
            || eventType == QByteArrayLiteral("windows_dispatcher_MSG"))
        && message && result) {
        const auto* nativeMessage = static_cast<MSG*>(message);
        if (nativeMessage->message == WM_NCHITTEST) {
            const HWND mainWindowHandle = reinterpret_cast<HWND>(winId());
            if (nativeMessage->hwnd != mainWindowHandle) {
                return QMainWindow::nativeEvent(eventType, message, result);
            }

            RECT windowRect {};
            if (!GetWindowRect(mainWindowHandle, &windowRect)) {
                return QMainWindow::nativeEvent(eventType, message, result);
            }

            const int globalX = GET_X_LPARAM(nativeMessage->lParam);
            const int globalY = GET_Y_LPARAM(nativeMessage->lParam);
            // Keep title-bar controls in the Qt client area even when their
            // upper edge falls inside the native resize hit-test band.
            if (m_topBar) {
                const auto controls = m_topBar->findChildren<QToolButton*>(
                    QString(), Qt::FindDirectChildrenOnly);
                const qreal deviceRatio = devicePixelRatioF();
                for (const auto* control : controls) {
                    if (!control->isVisible()) {
                        continue;
                    }
                    const QPoint logicalPosition = control->mapTo(this, QPoint(0, 0));
                    const QRect physicalRect(
                        windowRect.left + qRound(logicalPosition.x() * deviceRatio),
                        windowRect.top + qRound(logicalPosition.y() * deviceRatio),
                        qRound(control->width() * deviceRatio),
                        qRound(control->height() * deviceRatio));
                    if (physicalRect.contains(QPoint(globalX, globalY))) {
                        *result = HTCLIENT;
                        return true;
                    }
                }
            }

            // Use a fixed physical-pixel border. Scaling this value by the Qt
            // device ratio makes the hit band grow into the title-bar controls
            // on high-DPI displays.
            constexpr int kResizeHitTestMarginPx = 6;
            const int margin = kResizeHitTestMarginPx;
            const bool maximized = isMaximized() || isFullScreen();
            if (!maximized) {
                Qt::Edges edges;
                if (globalX < windowRect.left + margin) edges |= Qt::LeftEdge;
                if (globalX >= windowRect.right - margin) edges |= Qt::RightEdge;
                if (globalY < windowRect.top + margin) edges |= Qt::TopEdge;
                if (globalY >= windowRect.bottom - margin) edges |= Qt::BottomEdge;
                if (edges != Qt::Edges()) {
                    if (edges == (Qt::TopEdge | Qt::LeftEdge)) *result = HTTOPLEFT;
                    else if (edges == (Qt::TopEdge | Qt::RightEdge)) *result = HTTOPRIGHT;
                    else if (edges == (Qt::BottomEdge | Qt::LeftEdge)) *result = HTBOTTOMLEFT;
                    else if (edges == (Qt::BottomEdge | Qt::RightEdge)) *result = HTBOTTOMRIGHT;
                    else if (edges.testFlag(Qt::LeftEdge)) *result = HTLEFT;
                    else if (edges.testFlag(Qt::RightEdge)) *result = HTRIGHT;
                    else if (edges.testFlag(Qt::TopEdge)) *result = HTTOP;
                    else *result = HTBOTTOM;
                    return true;
                }
            }

            // Keep the whole non-resize area in the Qt client region. Returning
            // HTCAPTION here would route mouse input through Windows and prevent
            // the title-bar tool buttons from receiving their click events.
            *result = HTCLIENT;
            return true;
        }
    }
#else
    Q_UNUSED(eventType)
    Q_UNUSED(message)
    Q_UNUSED(result)
#endif
    return QMainWindow::nativeEvent(eventType, message, result);
}

void MainWindow::applyTitleBarMode(midi_play::settings::TitleBarMode mode)
{
    const auto normalizedMode = midi_play::settings::normalizeTitleBarMode(mode);
    const bool custom = normalizedMode == midi_play::settings::TitleBarMode::Custom;
    if (m_topBar) {
        m_topBar->setDragEnabled(custom);
    }
    if (m_titleBarMode == normalizedMode && windowFlags().testFlag(Qt::FramelessWindowHint)
        == custom) {
        updateWindowControlButtons();
        return;
    }

    const bool visible = isVisible();
    const bool maximized = isMaximized();
    const bool fullScreen = isFullScreen();
    const QRect savedNormalGeometry = normalGeometry();
    if (visible) hide();

    m_titleBarMode = normalizedMode;
    setWindowFlag(Qt::FramelessWindowHint, custom);
    m_windowControlsSeparator->setVisible(custom);
    m_minimizeButton->setVisible(custom);
    m_maximizeButton->setVisible(custom);
    m_closeButton->setVisible(custom);
    updateWindowControlButtons();

    if (visible) {
        if (fullScreen) showFullScreen();
        else if (maximized) showMaximized();
        else {
            show();
            if (savedNormalGeometry.isValid()) setGeometry(savedNormalGeometry);
        }
    }
}

void MainWindow::updateWindowControlButtons()
{
    if (!m_maximizeButton) return;
    const bool maximized = isMaximized();
    m_maximizeButton->setIcon(windowControlIcon(
        maximized ? WindowControlGlyph::Restore : WindowControlGlyph::Maximize));
    m_maximizeButton->setToolTip(maximized ? QStringLiteral("还原窗口") : QStringLiteral("最大化窗口"));
    m_maximizeButton->setAccessibleName(maximized ? QStringLiteral("还原窗口")
                                                  : QStringLiteral("最大化窗口"));
}

void MainWindow::openMusicFile()
{
    const QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("打开音乐文件"), {},
        QStringLiteral("音乐文件 (*.xml *.musicxml *.mid *.midi *.kar);;MusicXML (*.xml *.musicxml);;MIDI (*.mid *.midi *.kar);;所有文件 (*.*)"));
    if (!path.isEmpty()) m_service->openFile(path);
}

void MainWindow::showSettings()
{
    if (!m_settingsDialog) {
        m_settingsDialog = new settings::SettingsDialog(m_settingsService, m_service, this);
    }

    m_settingsDialog->show();
    m_settingsDialog->raise();
    m_settingsDialog->activateWindow();
}

void MainWindow::updatePosition(qint64 position, qint64 duration)
{
    m_positionUs = std::clamp<qint64>(position, 0, std::max<qint64>(0, duration));
    m_durationUs = std::max<qint64>(0, duration);

    // Position signals already queued before a blocking seek can arrive after
    // the mouse release. Keep the release point authoritative until the seek
    // transaction publishes its exact position.
    if (m_seekPending) {
        if (m_positionUs != m_pendingSeekUs) {
            return;
        }
        m_seekPending = false;
    }
    if (!m_sliderDragging) {
        const int sliderValue = m_durationUs > 0
            ? static_cast<int>(static_cast<long double>(m_positionUs) * kSliderResolution / m_durationUs)
            : 0;
        m_positionSlider->setValue(std::clamp(sliderValue, 0, kSliderResolution));
        updateTimeDisplay(m_positionUs, m_durationUs);
        updateMetadata(m_positionUs);
    }
    m_visualization->setTransportPosition(m_positionUs, m_durationUs);
}

void MainWindow::updatePlaybackState(playback::State state)
{
    m_playbackState = state;
    m_visualization->setTransportState(state);
    switch (state) {
    case playback::State::Playing: m_statusLabel->setText(QStringLiteral("播放中")); break;
    case playback::State::Paused: m_statusLabel->setText(QStringLiteral("已暂停")); break;
    case playback::State::Stopped: m_statusLabel->setText(QStringLiteral("已停止")); break;
    case playback::State::Ready: m_statusLabel->setText(QStringLiteral("准备播放")); break;
    case playback::State::Error: m_statusLabel->setText(QStringLiteral("播放错误")); break;
    case playback::State::Empty: m_statusLabel->setText(QStringLiteral("就绪")); break;
    }
    updateTransportControls();
}

QString MainWindow::formatTime(qint64 microseconds)
{
    const qint64 totalSeconds = std::max<qint64>(0, microseconds) / 1'000'000;
    const qint64 hours = totalSeconds / 3600;
    const qint64 minutes = (totalSeconds / 60) % 60;
    const qint64 seconds = totalSeconds % 60;
    if (hours > 0) {
        return QStringLiteral("%1:%2:%3").arg(hours).arg(minutes, 2, 10, QLatin1Char('0'))
            .arg(seconds, 2, 10, QLatin1Char('0'));
    }
    return QStringLiteral("%1:%2").arg(minutes, 2, 10, QLatin1Char('0'))
        .arg(seconds, 2, 10, QLatin1Char('0'));
}

void MainWindow::updateMetadata(qint64 positionUs)
{
    PlaybackMetadata metadata;
    if (!m_metadataTimeline.update(positionUs, metadata)) return;
    m_keyLabel->setText(metadata.key);
    m_timeSignatureLabel->setText(metadata.timeSignature);
    m_tempoLabel->setText(metadata.tempo);
}

void MainWindow::updateTimeDisplay(qint64 positionUs, qint64 durationUs)
{
    const qint64 positionSecond = std::max<qint64>(0, positionUs) / 1'000'000;
    const qint64 durationSecond = std::max<qint64>(0, durationUs) / 1'000'000;
    if (positionSecond == m_displayedPositionSecond
        && durationSecond == m_displayedDurationSecond) {
        return;
    }
    m_displayedPositionSecond = positionSecond;
    m_displayedDurationSecond = durationSecond;
    m_timeLabel->setText(QStringLiteral("%1 / %2")
        .arg(formatTime(positionUs), formatTime(durationUs)));
}

void MainWindow::updateResponsiveVisibility()
{
    // Preserve transport-critical information on narrow windows. The
    // SoundFont path remains available through its tooltip and settings.
    m_soundFontLabel->setVisible(width() >= 1080);
    m_keyLabel->setVisible(width() >= 860);
}

void MainWindow::updateTransportControls()
{
    const bool hasDocument = m_durationUs > 0 && m_playbackState != playback::State::Empty;
    m_playButton->setEnabled(hasDocument && m_playbackState != playback::State::Playing
                             && m_playbackState != playback::State::Error);
    m_pauseButton->setEnabled(m_playbackState == playback::State::Playing);
    m_stopButton->setEnabled(hasDocument && m_playbackState != playback::State::Stopped);
    m_positionSlider->setEnabled(hasDocument);
}

} // namespace midi_play::presentation
