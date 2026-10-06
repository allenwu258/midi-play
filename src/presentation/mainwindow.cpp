#include "mainwindow.h"

#include "app/audioexportservice.h"
#include "app/playerapplicationservice.h"
#include "app/settingsservice.h"
#include "app/ffmpegservice.h"
#include "app/videoexportservice.h"
#include "playbackmetadatapresenter.h"
#include "presentation/settings/settingsdialog.h"
#include "presentation/transport/playbackratecontrol.h"
#include "presentation/windowchrome/customtitlebar.h"
#include "presentation/windowchrome/windowchromecontroller.h"
#include "presentation/windowchrome/captionbuttons.h"
#include "presentation/visualization/fallingnotesview.h"
#include "presentation/videoexportdialog.h"

#include "presentation/theme/widgetstyles.h"
#include "presentation/theme/themeicons.h"
#include "presentation/theme/themecontroller.h"

#include <QFileDialog>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QEvent>
#include <QFormLayout>
#include <QFrame>
#include <QFutureWatcher>
#include <QHelpEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPainter>
#include <QPixmap>
#include <QProgressDialog>
#include <QPushButton>
#include <QResizeEvent>
#include <QSlider>
#include <QSpinBox>
#include <QStyle>
#include <QTimer>
#include <QToolButton>
#include <QToolTip>
#include <QVBoxLayout>
#include <QWidget>
#include <QtMath>
#include <QtConcurrent>

#include <algorithm>

namespace midi_play::presentation {
namespace {

class ReasonToolButton final : public QToolButton {
public:
    using QToolButton::QToolButton;

protected:
    bool event(QEvent* event) override
    {
        if (!isEnabled() && event->type() == QEvent::ToolTip && !toolTip().isEmpty()) {
            const auto* help = static_cast<QHelpEvent*>(event);
            QToolTip::showText(help->globalPos(), toolTip(), this);
            return true;
        }
        return QToolButton::event(event);
    }
};

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
    auto* button = new ReasonToolButton(parent);
    button->setIcon(icon);
    button->setText(text);
    button->setToolTip(tooltip);
    button->setToolButtonStyle(iconOnly ? Qt::ToolButtonIconOnly : Qt::ToolButtonTextBesideIcon);
    button->setAutoRaise(true);
    if (iconOnly) button->setFixedSize(38, 38);
    else button->setMinimumHeight(36);
    return button;
}

} // namespace

MainWindow::MainWindow(app::PlayerApplicationService* service,
                       app::SettingsService* settingsService,
                       QWidget* parent, theme::ThemeController* themeController,
                       app::FfmpegService* ffmpegService)
    : QMainWindow(parent), m_service(service), m_settingsService(settingsService), m_ffmpegService(ffmpegService)
    , m_themeController(themeController)
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
    m_windowChrome = new windowchrome::WindowChromeController(this);
    topBar->setObjectName(QStringLiteral("topBar"));
    topBar->setFixedHeight(58);
    auto* topLayout = new QHBoxLayout(topBar);
    topLayout->setContentsMargins(16, 0, 12, 0);
    topLayout->setSpacing(10);

    m_systemMenuButton = new QToolButton(topBar);
    m_systemMenuButton->setObjectName(QStringLiteral("windowSystemMenuButton"));
    m_systemMenuButton->setAutoRaise(true);
    // Keep the system-menu hit target generous while giving the bundled icon
    // enough pixels to remain legible at normal and fractional DPI scales.
    m_systemMenuButton->setFixedSize(32, 32);
    m_systemMenuButton->setIcon(windowIcon());
    m_systemMenuButton->setIconSize(QSize(28, 28));
    m_systemMenuButton->setAccessibleName(QStringLiteral("窗口系统菜单"));
    m_systemMenuButton->setToolTip(QStringLiteral("窗口系统菜单（Alt+Space）"));
    m_systemMenuButton->hide();
    topLayout->addWidget(m_systemMenuButton);

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
    m_tempoLabel->setToolTip(QStringLiteral("原曲当前位置的 BPM（不含播放倍率）"));
    topLayout->addWidget(m_keyLabel);
    topLayout->addWidget(m_timeSignatureLabel);
    topLayout->addWidget(m_tempoLabel);
    topLayout->addWidget(verticalSeparator(topBar));

    m_openButton = toolButton(topBar, QIcon(),
                                 QStringLiteral("打开乐曲"), QStringLiteral("打开 MusicXML 或 MIDI 文件"));
    m_exportButton = toolButton(topBar, QIcon(),
                                QStringLiteral("导出音频"), QStringLiteral("将当前乐曲导出为 MP3 或 WAV"));
    m_exportButton->setObjectName(QStringLiteral("exportButton"));
    m_videoExportButton = toolButton(topBar, QIcon(), QStringLiteral("导出视频"),
                                    QStringLiteral("仅支持 Vulkan 画面和有效 FFmpeg 时可用"));
    m_videoExportButton->setObjectName(QStringLiteral("videoExportButton"));
    m_settingsButton = toolButton(topBar, QIcon(),
                                      QStringLiteral("设置"), QStringLiteral("打开播放器设置"));
    topLayout->addWidget(m_openButton);
    topLayout->addWidget(m_exportButton);
    topLayout->addWidget(m_videoExportButton);
    topLayout->addWidget(m_settingsButton);

    m_windowChrome->setTitleBar(topBar, m_systemMenuButton,
        {m_openButton, m_exportButton, m_videoExportButton, m_settingsButton});
    m_captionButtons = new windowchrome::CaptionButtons(topBar);
    m_captionButtons->hide();
    topLayout->addWidget(m_captionButtons, 0, Qt::AlignTop);
    m_windowChrome->setCaptionButtons(m_captionButtons);
    connect(m_windowChrome, &windowchrome::WindowChromeController::frameMetricsChanged,
            this, &MainWindow::updateWindowChrome);
    connect(m_windowChrome, &windowchrome::WindowChromeController::modeChanged,
            this, &MainWindow::updateWindowChrome);
    connect(m_systemMenuButton, &QToolButton::clicked,
            m_windowChrome, &windowchrome::WindowChromeController::showSystemMenu);
    root->addWidget(topBar);

    m_visualization = new visualization::FallingNotesView(central);
    root->addWidget(m_visualization, 1);
    if (m_settingsService) {
        // A missing graphics preference starts with the Vulkan default. Keep
        // that preference only after the renderer has produced its first
        // frame; initialization failure records the traditional fallback.
        if (!m_settingsService->graphicsModeConfigured()) {
            connect(m_visualization, &visualization::FallingNotesView::graphicsModeResolved,
                    this, [this](midi_play::settings::GraphicsMode mode) {
                        if (m_settingsService && !m_settingsService->graphicsModeConfigured())
                            m_settingsService->setGraphicsMode(mode);
                    });
        }
        m_visualization->setGraphicsMode(m_settingsService->graphicsMode());
        m_visualization->setBackgroundImagePath(m_settingsService->activeBackgroundImagePath());
        m_visualization->setBackgroundImageAlignment(m_settingsService->backgroundImageAlignment());
        m_visualization->setBackgroundImageOpacity(m_settingsService->backgroundImageOpacity());
        m_visualization->setShowNotationStrip(m_settingsService->showNotationStrip());
        m_visualization->setVisualEffectsEnabled(m_settingsService->visualEffectsEnabled());
        m_visualization->setVisualEffectsLevel(m_settingsService->visualEffectsLevel());
        m_visualization->setNoteColorMode(m_settingsService->noteColorMode());
        connect(m_settingsService, &app::SettingsService::showNotationStripChanged,
                m_visualization, &visualization::FallingNotesView::setShowNotationStrip);
        connect(m_settingsService, &app::SettingsService::visualEffectsEnabledChanged,
                m_visualization, &visualization::FallingNotesView::setVisualEffectsEnabled);
        connect(m_settingsService, &app::SettingsService::visualEffectsLevelChanged,
                m_visualization, &visualization::FallingNotesView::setVisualEffectsLevel);
        connect(m_settingsService, &app::SettingsService::noteColorModeChanged,
                m_visualization, &visualization::FallingNotesView::setNoteColorMode);
        connect(m_settingsService, &app::SettingsService::graphicsModeChanged,
                m_visualization, &visualization::FallingNotesView::setGraphicsMode);
        const auto updateBackground = [this] {
            m_visualization->setBackgroundImagePath(m_settingsService->activeBackgroundImagePath());
        };
        connect(m_settingsService, &app::SettingsService::backgroundImagePathChanged,
                m_visualization, updateBackground);
        connect(m_settingsService, &app::SettingsService::backgroundImageEnabledChanged,
                m_visualization, updateBackground);
        connect(m_settingsService, &app::SettingsService::backgroundImageAlignmentChanged,
                m_visualization, &visualization::FallingNotesView::setBackgroundImageAlignment);
        connect(m_settingsService, &app::SettingsService::backgroundImageOpacityChanged,
                m_visualization, &visualization::FallingNotesView::setBackgroundImageOpacity);
    }

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

    m_timeLabel = new QLabel(QStringLiteral("00:00 / 00:00"), transport);
    m_timeLabel->setObjectName(QStringLiteral("timeLabel"));
    m_timeLabel->setAccessibleName(QStringLiteral("播放时间"));
    m_timeLabel->setMinimumWidth(118);
    m_timeLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

    auto* controlRow = new QHBoxLayout();
    controlRow->setContentsMargins(0, 0, 0, 0);
    controlRow->setSpacing(6);
    m_playButton = toolButton(transport, QIcon(), QStringLiteral("播放"),
                              QStringLiteral("播放"), true);
    m_pauseButton = toolButton(transport, QIcon(), QStringLiteral("暂停"),
                               QStringLiteral("暂停"), true);
    m_stopButton = toolButton(transport, QIcon(), QStringLiteral("停止"),
                              QStringLiteral("停止并回到开头"), true);
    m_playButton->setObjectName(QStringLiteral("playButton"));
    m_pauseButton->setObjectName(QStringLiteral("pauseButton"));
    m_stopButton->setObjectName(QStringLiteral("stopButton"));
    for (auto* button : {m_playButton, m_pauseButton, m_stopButton}) {
        button->setFixedSize(48, 44);
        button->setIconSize(QSize(26, 26));
    }
    m_playbackRateControl = new PlaybackRateControl(transport);
    m_playbackRateControl->setRatePercent(m_service ? m_service->playbackRatePercent()
                                                  : midi_play::settings::kDefaultPlaybackRatePercent);
    controlRow->addWidget(m_playButton);
    controlRow->addWidget(m_pauseButton);
    controlRow->addWidget(m_stopButton);
    controlRow->addWidget(m_playbackRateControl);
    m_metronomeButton = toolButton(transport, QIcon(), QStringLiteral("节拍器"),
                                   QStringLiteral("开启或关闭节拍器"));
    m_metronomeButton->setObjectName(QStringLiteral("metronomeButton"));
    m_metronomeButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
    m_metronomeButton->setCheckable(true);
    m_metronomeButton->setFixedSize(84, 44);
    m_metronomeButton->setAccessibleName(QStringLiteral("节拍器"));
    m_metronomeButton->setFocusPolicy(Qt::StrongFocus);
    m_metronomeButton->setEnabled(m_service && m_service->supportsMetronome());
    m_metronomeButton->setChecked(m_service && m_service->metronomeEnabled());
    m_metronomeButton->setToolTip(m_service && !m_service->supportsMetronome()
        ? m_service->metronomeUnavailableReason()
        : QStringLiteral("跟随乐曲节拍；小节首拍为重音，仅播放时发声"));
    controlRow->addWidget(m_metronomeButton);
    controlRow->addStretch();
    controlRow->addWidget(m_timeLabel);
    controlRow->addWidget(verticalSeparator(transport));
    m_statusLabel = new QLabel(QStringLiteral("就绪"), transport);
    m_statusLabel->setObjectName(QStringLiteral("statusLabel"));
    controlRow->addWidget(m_statusLabel);
    transportLayout->addLayout(controlRow);
    root->addWidget(transport);
    m_soundFontErrorLabel = new QLabel(central);
    m_soundFontErrorLabel->setObjectName(QStringLiteral("soundFontError"));
    m_soundFontErrorLabel->setTextFormat(Qt::PlainText);
    m_soundFontErrorLabel->setWordWrap(true);
    m_soundFontErrorLabel->setAccessibleName(QStringLiteral("音源错误"));
    m_soundFontErrorLabel->hide();
    root->addWidget(m_soundFontErrorLabel);
    setCentralWidget(central);

    applyTheme(m_themeController ? m_themeController->mode()
        : m_settingsService ? m_settingsService->themeMode() : midi_play::settings::kDefaultThemeMode);
    if (m_themeController) {
        connect(m_themeController, &theme::ThemeController::themeChanged, this, &MainWindow::applyTheme);
    } else if (m_settingsService) {
        connect(m_settingsService, &app::SettingsService::themeModeChanged, this, &MainWindow::applyTheme);
    }

    connect(m_openButton, &QToolButton::clicked, this, &MainWindow::openMusicFile);
    connect(m_exportButton, &QToolButton::clicked, this, &MainWindow::exportAudio);
    connect(m_videoExportButton, &QToolButton::clicked, this, &MainWindow::exportVideo);
    connect(m_settingsButton, &QToolButton::clicked, this, &MainWindow::showSettings);
    connect(m_windowChrome, &windowchrome::WindowChromeController::nativeIntegrationFailed,
            this, [this](const QString& reason) {
        m_statusLabel->setText(reason);
    });
    if (m_settingsService) {
        connect(m_settingsService, &app::SettingsService::titleBarModeChanged,
                this, &MainWindow::applyTitleBarMode);
    }
    if (m_ffmpegService)
        connect(m_ffmpegService, &app::FfmpegService::changed, this, &MainWindow::updateVideoExportAvailability);
    connect(m_visualization, &visualization::FallingNotesView::graphicsModeResolved,
            this, [this](midi_play::settings::GraphicsMode) { updateVideoExportAvailability(); });
    updateVideoExportAvailability();
    connect(m_playButton, &QToolButton::clicked, m_service, &app::PlayerApplicationService::play);
    connect(m_pauseButton, &QToolButton::clicked, m_service, &app::PlayerApplicationService::pause);
    connect(m_stopButton, &QToolButton::clicked, m_service, &app::PlayerApplicationService::stop);
    connect(m_metronomeButton, &QToolButton::toggled,
            m_service, &app::PlayerApplicationService::setMetronomeEnabled);
    connect(m_service, &app::PlayerApplicationService::metronomeChanged,
            this, [this](bool enabled) {
                const QSignalBlocker blocker(m_metronomeButton);
                m_metronomeButton->setChecked(enabled);
            });
    connect(m_service, &app::PlayerApplicationService::metronomeAvailabilityChanged,
            this, [this](bool available, const QString& reason) {
                m_metronomeButton->setEnabled(available);
                m_metronomeButton->setToolTip(available
                    ? QStringLiteral("跟随乐曲节拍；小节首拍为重音，仅播放时发声") : reason);
            });
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
                updateVideoExportAvailability();
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
    connect(m_service, &app::PlayerApplicationService::playbackRateChanged,
            m_playbackRateControl, &PlaybackRateControl::setRatePercent);
    connect(m_service, &app::PlayerApplicationService::playbackRateChanged,
            m_visualization, &visualization::FallingNotesView::setPlaybackRate);
    connect(m_service, &app::PlayerApplicationService::playbackDiscontinuity,
            m_visualization, &visualization::FallingNotesView::resetTransientEffects);
    m_visualization->setPlaybackRate(m_service->playbackRatePercent());
    m_visualization->setRefreshRate(m_service->visualizationRefreshRate());
    if (m_settingsService) {
        connect(m_settingsService, &app::SettingsService::visualizationRefreshRateChanged,
                m_visualization, &visualization::FallingNotesView::setRefreshRate);
    }
    connect(m_playbackRateControl, &PlaybackRateControl::ratePercentEdited,
            m_service, &app::PlayerApplicationService::setPlaybackRatePercent);
    connect(m_service, &app::PlayerApplicationService::soundFontLoaded, this, [this] {
        m_statusLabel->setText(QStringLiteral("音源已加载"));
        m_soundFontErrorLabel->clear();
        m_soundFontErrorLabel->hide();
    });
    connect(m_service, &app::PlayerApplicationService::soundFontLoadFailed,
            this, [this](const QString& message) {
                m_statusLabel->setText(QStringLiteral("请检查音源设置"));
                m_soundFontErrorLabel->setText(message);
                m_soundFontErrorLabel->show();
            });
    connect(m_service, &app::PlayerApplicationService::soundFontLoadingChanged, this, [this](bool loading) {
        m_openButton->setEnabled(!loading);
        if (loading) m_statusLabel->setText(QStringLiteral("正在检查音源…"));
        updateTransportControls();
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
        if (!m_settingsService->lastLoadWarning().isEmpty())
            m_statusLabel->setText(m_settingsService->lastLoadWarning());
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
        updateWindowChrome();
    } else if (event->type() == QEvent::ActivationChange && m_topBar) {
        m_topBar->setWindowActive(isActiveWindow());
    } else if (event->type() == QEvent::WindowIconChange && m_systemMenuButton) {
        m_systemMenuButton->setIcon(windowIcon());
    }
}

bool MainWindow::nativeEvent(const QByteArray& eventType, void* message, qintptr* result)
{
    if (m_windowChrome && m_windowChrome->processNativeEvent(eventType, message, result))
        return true;
    return QMainWindow::nativeEvent(eventType, message, result);
}

void MainWindow::applyTitleBarMode(midi_play::settings::TitleBarMode mode)
{
    m_windowChrome->setMode(mode);
    updateWindowChrome();
}

void MainWindow::updateWindowChrome()
{
    if (!m_topBar || !m_windowChrome || !m_systemMenuButton) return;
    const bool custom = m_windowChrome->mode() == midi_play::settings::TitleBarMode::Custom;
    m_systemMenuButton->setVisible(custom && !isFullScreen());
    m_captionButtons->setVisible(custom && !isFullScreen());
    m_captionButtons->setMaximized(isMaximized());
    m_topBar->layout()->setContentsMargins(16, 0, custom ? 0 : 12, 0);
    m_topBar->setFixedHeight(std::max(58, m_windowChrome->minimumCaptionHeight()));
}

void MainWindow::applyTheme(midi_play::settings::ThemeMode mode)
{
    m_themeMode = midi_play::settings::normalizeThemeMode(mode);
    const auto& current = theme::themeFor(m_themeMode);
    setPalette(theme::widgetPalette(current));
    setStyleSheet(theme::mainWindowStyle(current));
    m_visualization->setThemeMode(m_themeMode);
    m_playbackRateControl->setThemeMode(m_themeMode);
    m_windowChrome->setTheme(m_themeMode);
    m_openButton->setIcon(theme::themedIcon(theme::IconGlyph::Open, current));
    m_exportButton->setIcon(theme::themedIcon(theme::IconGlyph::Export, current));
    m_settingsButton->setIcon(theme::themedIcon(theme::IconGlyph::Settings, current));
    m_playButton->setIcon(theme::themedIcon(theme::IconGlyph::Play, current));
    m_pauseButton->setIcon(theme::themedIcon(theme::IconGlyph::Pause, current));
    m_stopButton->setIcon(theme::themedIcon(theme::IconGlyph::Stop, current));
}

void MainWindow::openMusicFile()
{
    const QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("打开音乐文件"), {},
        QStringLiteral("音乐文件 (*.xml *.musicxml *.mid *.midi *.kar);;MusicXML (*.xml *.musicxml);;MIDI (*.mid *.midi *.kar);;所有文件 (*.*)"));
    if (!path.isEmpty()) m_service->openFile(path);
}

MainWindow::~MainWindow()
{
    if (m_exportCancel) m_exportCancel->store(true);
}

void MainWindow::exportAudio()
{
    const auto document = m_service->document();
    if (!document || m_exporting) return;
    const QString soundFont = m_service->soundFontPath();
    if (soundFont.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("导出音频"),
                             QStringLiteral("请先在设置中选择 SF2/SF3 音源。"));
        return;
    }

    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("导出音频"));
    dialog.setMinimumWidth(470);
    auto* form = new QFormLayout(&dialog);
    auto* song = new QLabel(QFileInfo(m_service->fileName()).fileName(), &dialog);
    auto* font = new QLabel(QFileInfo(soundFont).fileName(), &dialog);
    song->setToolTip(m_service->fileName());
    font->setToolTip(soundFont);
    form->addRow(QStringLiteral("乐曲"), song);
    form->addRow(QStringLiteral("音源"), font);
    auto* format = new QComboBox(&dialog);
    format->addItem(QStringLiteral("MP3"));
    format->addItem(QStringLiteral("WAV (16-bit PCM)"));
    form->addRow(QStringLiteral("格式"), format);
    auto* bitrate = new QComboBox(&dialog);
    for (int value : {128, 160, 192, 256, 320})
        bitrate->addItem(QStringLiteral("%1 kbps").arg(value), value);
    bitrate->setCurrentIndex(2);
    form->addRow(QStringLiteral("MP3 码率"), bitrate);
    connect(format, &QComboBox::currentIndexChanged, bitrate,
            [bitrate](int index) { bitrate->setEnabled(index == 0); });
    auto* sampleRate = new QComboBox(&dialog);
    sampleRate->addItem(QStringLiteral("44.1 kHz"), 44100);
    sampleRate->addItem(QStringLiteral("48 kHz"), 48000);
    form->addRow(QStringLiteral("采样率"), sampleRate);
    auto* metronome = new QCheckBox(QStringLiteral("包含节拍器"), &dialog);
    form->addRow(QString(), metronome);
    auto* tail = new QSpinBox(&dialog);
    tail->setRange(0, 5000);
    tail->setSingleStep(250);
    tail->setSuffix(QStringLiteral(" ms"));
    tail->setValue(500);
    form->addRow(QStringLiteral("尾音"), tail);
    auto* pathRow = new QWidget(&dialog);
    auto* pathLayout = new QHBoxLayout(pathRow);
    pathLayout->setContentsMargins(0, 0, 0, 0);
    auto* path = new QLineEdit(pathRow);
    path->setText(QFileInfo(m_service->fileName()).absolutePath() + QLatin1Char('/')
                  + QFileInfo(m_service->fileName()).completeBaseName() + QStringLiteral(".mp3"));
    auto* browse = new QPushButton(QStringLiteral("浏览…"), pathRow);
    pathLayout->addWidget(path, 1);
    pathLayout->addWidget(browse);
    form->addRow(QStringLiteral("输出文件"), pathRow);
    connect(format, &QComboBox::currentIndexChanged, path, [path](int index) {
        const QFileInfo current(path->text());
        if (current.suffix().compare(QStringLiteral("mp3"), Qt::CaseInsensitive) == 0
            || current.suffix().compare(QStringLiteral("wav"), Qt::CaseInsensitive) == 0) {
            path->setText(current.absolutePath() + QLatin1Char('/') + current.completeBaseName()
                          + (index == 0 ? QStringLiteral(".mp3") : QStringLiteral(".wav")));
        }
    });
    connect(browse, &QPushButton::clicked, &dialog, [&] {
        const QString selected = QFileDialog::getSaveFileName(
            &dialog, QStringLiteral("导出音频"), path->text(),
            format->currentIndex() == 0 ? QStringLiteral("MP3 音频 (*.mp3)")
                                        : QStringLiteral("WAV 音频 (*.wav)"));
        if (!selected.isEmpty()) path->setText(selected);
    });
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("开始导出"));
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted) return;

    app::AudioExportOptions options;
    options.format = format->currentIndex() == 0
        ? encoding::AudioFileFormat::Mp3 : encoding::AudioFileFormat::Wav;
    options.sampleRate = sampleRate->currentData().toInt();
    options.bitrateKbps = bitrate->currentData().toInt();
    options.includeMetronome = metronome->isChecked();
    options.tailMilliseconds = tail->value();
    options.soundFontPath = soundFont;
    options.outputPath = QFileInfo(path->text().trimmed()).absoluteFilePath();
    const QString expectedSuffix = options.format == encoding::AudioFileFormat::Mp3
        ? QStringLiteral("mp3") : QStringLiteral("wav");
    if (path->text().trimmed().isEmpty()
        || QFileInfo(options.outputPath).suffix().compare(expectedSuffix, Qt::CaseInsensitive) != 0) {
        QMessageBox::warning(this, QStringLiteral("导出音频"),
                             QStringLiteral("输出文件扩展名需要与所选格式一致。"));
        return;
    }
    if (QFileInfo::exists(options.outputPath)
        && QMessageBox::question(this, QStringLiteral("覆盖文件"),
            QStringLiteral("目标文件已存在，确定覆盖吗？\n%1").arg(options.outputPath))
            != QMessageBox::Yes) return;

    m_exporting = true;
    m_exportButton->setEnabled(false);
    updateVideoExportAvailability();
    m_exportCancel = std::make_shared<std::atomic_bool>(false);
    auto* progress = new QProgressDialog(QStringLiteral("正在导出音频"), QStringLiteral("取消"),
                                         0, 100, this);
    progress->setWindowTitle(QStringLiteral("导出音频"));
    progress->setWindowModality(Qt::NonModal);
    progress->setMinimumDuration(0);
    progress->setAutoClose(false);
    progress->setAutoReset(false);
    m_exportProgress = progress;
    const auto cancel = m_exportCancel;
    connect(progress, &QProgressDialog::canceled, this, [cancel] { cancel->store(true); });
    progress->show();

    auto* watcher = new QFutureWatcher<app::AudioExportResult>(this);
    auto renderedPercent = std::make_shared<std::atomic_int>(0);
    auto* progressTimer = new QTimer(this);
    progressTimer->setInterval(80);
    connect(progressTimer, &QTimer::timeout, this, [this, renderedPercent] {
        if (m_exportProgress && !m_exportProgress->wasCanceled())
            m_exportProgress->setValue(renderedPercent->load());
    });
    progressTimer->start();
    connect(watcher, &QFutureWatcher<app::AudioExportResult>::finished, this,
            [this, watcher, progressTimer, options] {
        const auto result = watcher->result();
        watcher->deleteLater();
        progressTimer->stop();
        progressTimer->deleteLater();
        if (m_exportProgress) m_exportProgress->deleteLater();
        m_exportProgress = nullptr;
        m_exportCancel.reset();
        m_exporting = false;
        updateTransportControls();
        updateVideoExportAvailability();
        if (result.status == app::AudioExportStatus::Success) {
            m_statusLabel->setText(result.clippedSamples
                ? QStringLiteral("音频已导出（检测到削波）: %1").arg(options.outputPath)
                : QStringLiteral("音频已导出: %1").arg(options.outputPath));
        } else if (result.status == app::AudioExportStatus::Canceled) {
            m_statusLabel->setText(QStringLiteral("已取消音频导出"));
        } else {
            m_statusLabel->setText(QStringLiteral("音频导出失败"));
            QMessageBox::warning(this, QStringLiteral("音频导出失败"), result.error);
        }
    });
    watcher->setFuture(QtConcurrent::run([document, options, cancel, renderedPercent] {
        return app::AudioExportService::exportDocument(document, options, cancel.get(),
            [renderedPercent](int percent) { renderedPercent->store(percent); });
    }));
}

void MainWindow::updateVideoExportAvailability()
{
    if (!m_videoExportButton) return;
    const bool document = m_service && m_service->document();
    const bool vulkan = m_visualization && m_visualization->vulkanReady();
    const bool ffmpeg = m_ffmpegService && m_ffmpegService->result().valid;
    const bool available = document && vulkan && ffmpeg && !m_exporting;
    m_videoExportButton->setEnabled(available);
    if (available) {
        m_videoExportButton->setToolTip(QStringLiteral("导出 Vulkan 流光画面为 MP4"));
    } else if (!vulkan) {
        m_videoExportButton->setToolTip(QStringLiteral("视频导出仅支持 Vulkan 图形模式"));
    } else if (m_ffmpegService && m_ffmpegService->checking()) {
        m_videoExportButton->setToolTip(QStringLiteral("正在检查 FFmpeg"));
    } else if (!ffmpeg && m_ffmpegService) {
        m_videoExportButton->setToolTip(m_ffmpegService->unavailableReason());
    } else if (!document) {
        m_videoExportButton->setToolTip(QStringLiteral("请先打开乐曲"));
    } else if (m_exporting) {
        m_videoExportButton->setToolTip(QStringLiteral("正在导出，请稍候"));
    } else {
        m_videoExportButton->setToolTip(QStringLiteral("视频导出服务不可用"));
    }
}

void MainWindow::exportVideo()
{
#if !MIDI_PLAY_HAS_VULKAN
    return;
#else
    const auto document = m_service ? m_service->document() : nullptr;
    if (!document || m_exporting || !m_ffmpegService || !m_ffmpegService->result().valid
        || !m_visualization->vulkanReady()) return;
    app::VideoExportOptions defaults;
    defaults.ffmpegExecutable = m_ffmpegService->result().executablePath;
    defaults.sourcePath = m_service->fileName();
    defaults.soundFontPath = m_service->soundFontPath();
    defaults.scene.state = m_visualization->exportState();
    defaults.scene.logicalSize = QSize(1280, 720);
    defaults.scene.outputSize = QSize(1920, 1080);
    defaults.scene.font = m_visualization->font();
    defaults.scene.background = m_visualization->exportBackground();
    defaults.outputPath = QFileInfo(m_service->fileName()).absolutePath() + QLatin1Char('/')
        + QFileInfo(m_service->fileName()).completeBaseName() + QStringLiteral(".mp4");
    VideoExportDialog dialog(defaults, m_positionUs, this);
    if (dialog.exec() != QDialog::Accepted) return;
    auto options = dialog.options();
    options.ffmpegExecutable = m_ffmpegService->result().executablePath;
    if (options.outputPath.trimmed().isEmpty() || QFileInfo(options.outputPath).suffix().compare(QStringLiteral("mp4"), Qt::CaseInsensitive) != 0) {
        QMessageBox::warning(this, QStringLiteral("导出视频"), QStringLiteral("输出文件扩展名需要为 .mp4。"));
        return;
    }
    if (QFileInfo::exists(options.outputPath)
        && QMessageBox::question(this, QStringLiteral("覆盖文件"), QStringLiteral("目标文件已存在，确定覆盖吗？\n%1").arg(options.outputPath)) != QMessageBox::Yes) return;
    m_exporting = true;
    m_exportCancel = std::make_shared<std::atomic_bool>(false);
    auto* progress = new QProgressDialog(QStringLiteral("正在导出视频"), QStringLiteral("取消"), 0, 100, this);
    progress->setWindowTitle(QStringLiteral("导出视频")); progress->setMinimumDuration(0); progress->setAutoClose(false); progress->setAutoReset(false);
    m_exportProgress = progress;
    const auto cancel = m_exportCancel;
    auto* watcher = new QFutureWatcher<app::VideoExportResult>(this);
    auto stage = std::make_shared<std::atomic_int>(0);
    auto* timer = new QTimer(this); timer->setInterval(80);
    connect(timer, &QTimer::timeout, this, [this, stage] { if (m_exportProgress) m_exportProgress->setValue(stage->load()); });
    connect(progress, &QProgressDialog::canceled, this, [cancel] { cancel->store(true); });
    progress->show(); timer->start(); updateVideoExportAvailability();
    connect(watcher, &QFutureWatcher<app::VideoExportResult>::finished, this, [this, watcher, timer, options] {
        const auto result = watcher->result(); watcher->deleteLater(); timer->stop(); timer->deleteLater();
        if (m_exportProgress) m_exportProgress->deleteLater(); m_exportProgress = nullptr;
        m_exportCancel.reset(); m_exporting = false; updateTransportControls(); updateVideoExportAvailability();
        if (result.status == app::VideoExportStatus::Success) m_statusLabel->setText(QStringLiteral("视频已导出: %1").arg(options.outputPath));
        else if (result.status == app::VideoExportStatus::Canceled) m_statusLabel->setText(QStringLiteral("已取消视频导出"));
        else { m_statusLabel->setText(QStringLiteral("视频导出失败")); QMessageBox::warning(this, QStringLiteral("视频导出失败"), result.error); }
    });
    const auto frozenDocument = document;
    watcher->setFuture(QtConcurrent::run([frozenDocument, options, cancel, stage] {
        return app::VideoExportService::exportDocument(frozenDocument, options, cancel.get(),
            [stage](int percent, const QString&) { stage->store(percent); });
    }));
#endif
}

void MainWindow::showSettings()
{
    if (!m_settingsDialog) {
        m_settingsDialog = new settings::SettingsDialog(m_settingsService, m_service, this, m_themeController, m_ffmpegService);
    }

    m_settingsDialog->show();
    m_settingsDialog->raise();
    m_settingsDialog->activateWindow();
}

void MainWindow::updatePosition(qint64 position, qint64 duration, qint64 sampledAtUs)
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
    m_visualization->setTransportPosition(m_positionUs, m_durationUs, sampledAtUs);
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
    // Keep the title bar usable on narrow windows. The detailed musical
    // metadata is hidden before it competes with the file and action controls.
    const bool showMetadata = width() >= 860;
    m_keyLabel->setVisible(showMetadata);
    m_timeSignatureLabel->setVisible(showMetadata);
    m_tempoLabel->setVisible(showMetadata);
}

void MainWindow::updateTransportControls()
{
    const bool hasDocument = m_durationUs > 0 && m_playbackState != playback::State::Empty;
    m_playButton->setEnabled(hasDocument && m_playbackState != playback::State::Playing
                             && m_playbackState != playback::State::Error
                             && !m_service->isSoundFontLoading());
    m_pauseButton->setEnabled(m_playbackState == playback::State::Playing);
    m_stopButton->setEnabled(hasDocument && m_playbackState != playback::State::Stopped);
    m_positionSlider->setEnabled(hasDocument);
    m_exportButton->setEnabled(m_service->document() && !m_exporting);
    updateVideoExportAvailability();
}

} // namespace midi_play::presentation
