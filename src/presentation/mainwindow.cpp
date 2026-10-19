// 主窗口实现：连接应用服务与界面，维护播放交互、设置同步及异步导出生命周期。
#include "mainwindow.h"

#include "app/audioexportservice.h"
#include "app/playerapplicationservice.h"
#include "app/settingsservice.h"
#include "app/ffmpegservice.h"
#include "app/videoexportservice.h"
#include "presentation/exportdialog.h"
#include "playbackmetadatapresenter.h"
#include "presentation/settings/settingsdialog.h"
#include "presentation/transport/playbackratecontrol.h"
#include "presentation/windowchrome/customtitlebar.h"
#include "presentation/windowchrome/windowchromecontroller.h"
#include "presentation/windowchrome/captionbuttons.h"
#include "presentation/visualization/fallingnotesview.h"

#include "presentation/theme/widgetstyles.h"
#include "presentation/theme/themeicons.h"
#include "presentation/theme/themecontroller.h"

#include <QFileDialog>
#include <QDialog>
#include <QEvent>
#include <QFrame>
#include <QFutureWatcher>
#include <QHelpEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPainter>
#include <QPixmap>
#include <QProgressDialog>
#include <QResizeEvent>
#include <QSlider>
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

// 禁用时仍展示工具提示，用于说明导出或播放按钮暂不可用的原因。
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

// 创建使用统一样式对象名的工具栏分隔线。
QFrame* verticalSeparator(QWidget* parent)
{
    auto* separator = new QFrame(parent);
    separator->setFrameShape(QFrame::VLine);
    separator->setFrameShadow(QFrame::Plain);
    separator->setObjectName(QStringLiteral("toolbarSeparator"));
    return separator;
}

// 统一普通操作按钮的图标、提示和尺寸；纯图标按钮保持固定点击区域。
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

// 构建界面并绑定应用/设置服务；控件初值先由快照回填，再接收后续变更。
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

    // 顶栏在原生和自定义标题栏模式下复用，窗口装饰控制器处理系统交互。
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
    // 保留足够的系统菜单点击区域和图标像素，以适应普通及非整数 DPI 缩放。
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
                                QStringLiteral("导出"), QStringLiteral("打开音频或视频导出页面"));
    m_exportButton->setObjectName(QStringLiteral("exportButton"));
    m_settingsButton = toolButton(topBar, QIcon(),
                                      QStringLiteral("设置"), QStringLiteral("打开播放器设置"));
    topLayout->addWidget(m_openButton);
    topLayout->addWidget(m_exportButton);
    topLayout->addWidget(m_settingsButton);

    m_windowChrome->setTitleBar(topBar, m_systemMenuButton,
        {m_openButton, m_exportButton, m_settingsButton});
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
        // 首次使用以 Vulkan 为默认候选；首帧成功后确认偏好，初始化失败则记录传统模式。
        if (!m_settingsService->graphicsModeConfigured()) {
            connect(m_visualization, &visualization::FallingNotesView::graphicsModeResolved,
                    this, [this](midi_play::settings::GraphicsMode mode) {
                        if (m_settingsService && !m_settingsService->graphicsModeConfigured())
                            m_settingsService->setGraphicsMode(mode);
                    });
        }
        m_visualization->setGraphicsMode(m_settingsService->graphicsMode());
        m_visualization->setBackgroundImageMaximumDimension(
            m_settingsService->backgroundImageMaximumDimension());
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
        // 路径或开关变化都重新读取有效背景路径，关闭背景时向视图传入空路径。
        const auto updateBackground = [this] {
            m_visualization->setBackgroundImagePath(m_settingsService->activeBackgroundImagePath());
        };
        connect(m_settingsService, &app::SettingsService::backgroundImagePathChanged,
                m_visualization, updateBackground);
        connect(m_settingsService, &app::SettingsService::backgroundImageEnabledChanged,
                m_visualization, updateBackground);
        connect(m_settingsService, &app::SettingsService::backgroundImageMaximumDimensionChanged,
                m_visualization, &visualization::FallingNotesView::setBackgroundImageMaximumDimension);
        connect(m_settingsService, &app::SettingsService::backgroundImageAlignmentChanged,
                m_visualization, &visualization::FallingNotesView::setBackgroundImageAlignment);
        connect(m_settingsService, &app::SettingsService::backgroundImageOpacityChanged,
                m_visualization, &visualization::FallingNotesView::setBackgroundImageOpacity);
    }

    // 传输栏将固定刻度进度条与播放、倍率、节拍器控制集中在底部。
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
    connect(m_exportButton, &QToolButton::clicked, this, &MainWindow::openExportDialog);
    connect(m_settingsButton, &QToolButton::clicked, this, &MainWindow::showSettings);
    connect(m_windowChrome, &windowchrome::WindowChromeController::nativeIntegrationFailed,
            this, [this](const QString& reason) {
        m_statusLabel->setText(reason);
        // 集成失败时控制器已回退原生装饰；持久化有效模式并同步设置页，避免下次重复失败。
        if (m_settingsService
            && m_settingsService->titleBarMode() != midi_play::settings::TitleBarMode::Native) {
            m_settingsService->setTitleBarMode(midi_play::settings::TitleBarMode::Native);
        }
    });
    if (m_settingsService) {
        connect(m_settingsService, &app::SettingsService::titleBarModeChanged,
                this, &MainWindow::applyTitleBarMode);
    }
    updateExportButtonState();
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
    // 拖动阶段只预览标签；释放时提交一次 seek 并等待精确位置确认。
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

    // 应用服务先发布图表，再通知文档；文档通知重置秒级缓存和 seek 状态。
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
                updateExportButtonState();
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
    // seek、停止等时间跳变清除瞬态特效，防止保留旧位置的粒子状态。
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
    // 区分音源检查、导入忙状态和失败；音源检查期间阻止打开文件及开始播放。
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

// 关闭窗口只提出协作取消请求；后台任务持有独立快照和共享取消标志。
MainWindow::~MainWindow()
{
    if (m_exportCancel) m_exportCancel->store(true);
}

// 按当前后端/编码器能力构建参数页，并为视频冻结场景配置。
void MainWindow::openExportDialog()
{
    // 视频需要当前 Vulkan 视图已就绪且 FFmpeg 检测通过，音频入口独立可用。
    const auto document = m_service ? m_service->document() : nullptr;
    if (!document || m_exporting) return;
    const bool vulkan = m_visualization && m_visualization->vulkanReady();
    const bool ffmpeg = m_ffmpegService && m_ffmpegService->result().valid;
    const bool videoAvailable = vulkan && ffmpeg;
    QString videoUnavailableReason;
    if (!vulkan) videoUnavailableReason = QStringLiteral("视频导出仅支持 Vulkan 图形模式");
    else if (m_ffmpegService && m_ffmpegService->checking())
        videoUnavailableReason = QStringLiteral("正在检查 FFmpeg");
    else if (!ffmpeg && m_ffmpegService)
        videoUnavailableReason = m_ffmpegService->unavailableReason();
    else videoUnavailableReason = QStringLiteral("视频导出服务不可用");

    // 在打开参数页前快照主题、图表、背景和字体；最终导出使用独立离屏场景。
    app::VideoExportOptions videoDefaults;
    videoDefaults.ffmpegExecutable = m_ffmpegService ? m_ffmpegService->result().executablePath : QString();
    videoDefaults.sourcePath = m_service->fileName();
    videoDefaults.soundFontPath = m_service->soundFontPath();
    videoDefaults.scene.state = m_visualization->exportState();
    videoDefaults.scene.logicalSize = QSize(1280, 720);
    videoDefaults.scene.outputSize = QSize(1920, 1080);
    videoDefaults.scene.font = m_visualization->font();
    videoDefaults.scene.background = m_visualization->exportBackground();
    videoDefaults.outputPath = QFileInfo(m_service->fileName()).absolutePath() + QLatin1Char('/')
        + QFileInfo(m_service->fileName()).completeBaseName() + QStringLiteral(".mp4");

    ExportDialog dialog(m_service->fileName(), m_service->soundFontPath(), videoDefaults,
                        m_positionUs, videoAvailable, videoUnavailableReason, this);
    if (dialog.exec() != QDialog::Accepted) return;
    if (dialog.exportType() == ExportDialog::ExportType::Audio) {
        startAudioExport(dialog.audioOptions());
        return;
    }
    auto options = dialog.videoOptions();
    options.ffmpegExecutable = videoDefaults.ffmpegExecutable;
    startVideoExport(options);
}

// 启动唯一后台音频任务，GUI 采样原子进度，完成回调统一回收任务界面。
void MainWindow::startAudioExport(const app::AudioExportOptions& options)
{
    // 导出持有只读文档快照，后续切换播放曲目不影响当前任务。
    const auto document = m_service ? m_service->document() : nullptr;
    if (!document || m_exporting) return;
    m_exporting = true;
    updateExportButtonState();
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
    // 工作线程只写原子进度，GUI 每 80 ms 采样一次，避免跨线程访问控件。
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
        // 先释放任务相关界面状态，再按成功、取消或失败反馈结果。
        const auto result = watcher->result();
        watcher->deleteLater();
        progressTimer->stop();
        progressTimer->deleteLater();
        if (m_exportProgress) m_exportProgress->deleteLater();
        m_exportProgress = nullptr;
        m_exportCancel.reset();
        m_exporting = false;
        updateTransportControls();
        updateExportButtonState();
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
    // 所需数据均按值捕获；窗口关闭后任务仍能读取共享取消标志。
    watcher->setFuture(QtConcurrent::run([document, options, cancel, renderedPercent] {
        return app::AudioExportService::exportDocument(document, options, cancel.get(),
            [renderedPercent](int percent) { renderedPercent->store(percent); });
    }));
}

// 文档和导出占用共同决定入口是否可用，禁用时提供对应原因。
void MainWindow::updateExportButtonState()
{
    if (!m_exportButton) return;
    const bool available = m_service && m_service->document() && !m_exporting;
    m_exportButton->setEnabled(available);
    m_exportButton->setToolTip(m_exporting
        ? QStringLiteral("正在导出，请稍候")
        : available ? QStringLiteral("打开音频或视频导出页面") : QStringLiteral("请先打开乐曲"));
}

// Vulkan 构建中启动独立离屏视频导出；进度与取消均通过共享原子值传递。
void MainWindow::startVideoExport(const app::VideoExportOptions& options)
{
#if !MIDI_PLAY_HAS_VULKAN
    Q_UNUSED(options)
    return;
#else
    // 与音频导出共享单任务入口；独立文档快照供后台渲染和编码使用。
    const auto document = m_service ? m_service->document() : nullptr;
    if (!document || m_exporting) return;
    m_exporting = true;
    m_exportCancel = std::make_shared<std::atomic_bool>(false);
    auto* progress = new QProgressDialog(QStringLiteral("正在导出视频"), QStringLiteral("取消"), 0, 100, this);
    progress->setWindowTitle(QStringLiteral("导出视频")); progress->setMinimumDuration(0); progress->setAutoClose(false); progress->setAutoReset(false);
    m_exportProgress = progress;
    const auto cancel = m_exportCancel;
    auto* watcher = new QFutureWatcher<app::VideoExportResult>(this);
    // 进度仅通过原子整数传递到 GUI，阶段描述由应用服务生成但此处不展示。
    auto stage = std::make_shared<std::atomic_int>(0);
    auto* timer = new QTimer(this); timer->setInterval(80);
    connect(timer, &QTimer::timeout, this, [this, stage] { if (m_exportProgress) m_exportProgress->setValue(stage->load()); });
    connect(progress, &QProgressDialog::canceled, this, [cancel] { cancel->store(true); });
    progress->show(); timer->start(); updateExportButtonState();
    connect(watcher, &QFutureWatcher<app::VideoExportResult>::finished, this, [this, watcher, timer, options] {
        const auto result = watcher->result(); watcher->deleteLater(); timer->stop(); timer->deleteLater();
        if (m_exportProgress) m_exportProgress->deleteLater(); m_exportProgress = nullptr;
        m_exportCancel.reset(); m_exporting = false; updateTransportControls(); updateExportButtonState();
        if (result.status == app::VideoExportStatus::Success) m_statusLabel->setText(QStringLiteral("视频已导出: %1").arg(options.outputPath));
        else if (result.status == app::VideoExportStatus::Canceled) m_statusLabel->setText(QStringLiteral("已取消视频导出"));
        else { m_statusLabel->setText(QStringLiteral("视频导出失败")); QMessageBox::warning(this, QStringLiteral("视频导出失败"), result.error); }
    });
    // 强引用保留本次文档，当前播放会话随后切换也不会替换导出数据。
    const auto frozenDocument = document;
    watcher->setFuture(QtConcurrent::run([frozenDocument, options, cancel, stage] {
        return app::VideoExportService::exportDocument(frozenDocument, options, cancel.get(),
            [stage](int percent, const QString&) { stage->store(percent); });
    }));
#endif
}

// 复用非模态设置窗口，QPointer 在窗口销毁后自动清空。
void MainWindow::showSettings()
{
    if (!m_settingsDialog) {
        m_settingsDialog = new settings::SettingsDialog(m_settingsService, m_service, this, m_themeController, m_ffmpegService);
    }

    m_settingsDialog->show();
    m_settingsDialog->raise();
    m_settingsDialog->activateWindow();
}

// 消费实际传输时间；seek 待确认时丢弃旧展示更新，拖动时保留预览标签。
void MainWindow::updatePosition(qint64 position, qint64 duration, qint64 sampledAtUs)
{
    m_positionUs = std::clamp<qint64>(position, 0, std::max<qint64>(0, duration));
    m_durationUs = std::max<qint64>(0, duration);

    // seek 前排队的位置通知可能在鼠标释放后到达；精确目标发布前维持用户松开位置。
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
    // 采样时刻一并交给视图，用于连续播放期间按播放倍率外推当前位置。
    m_visualization->setTransportPosition(m_positionUs, m_durationUs, sampledAtUs);
}

// 将领域状态同步到可视化、状态文字和传输按钮，不在展示层触发播放转换。
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

// 秒级值未变化时跳过标签重排，底层仍保留微秒精度的位置。
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
    // 窄窗口优先保留文件信息和操作按钮，隐藏详细音乐元数据。
    const bool showMetadata = width() >= 860;
    m_keyLabel->setVisible(showMetadata);
    m_timeSignatureLabel->setVisible(showMetadata);
    m_tempoLabel->setVisible(showMetadata);
}

// 播放入口额外受音源加载状态约束，暂停、停止及进度条按会话状态更新。
void MainWindow::updateTransportControls()
{
    const bool hasDocument = m_durationUs > 0 && m_playbackState != playback::State::Empty;
    m_playButton->setEnabled(hasDocument && m_playbackState != playback::State::Playing
                             && m_playbackState != playback::State::Error
                             && !m_service->isSoundFontLoading());
    m_pauseButton->setEnabled(m_playbackState == playback::State::Playing);
    m_stopButton->setEnabled(hasDocument && m_playbackState != playback::State::Stopped);
    m_positionSlider->setEnabled(hasDocument);
    updateExportButtonState();
}

} // namespace midi_play::presentation
