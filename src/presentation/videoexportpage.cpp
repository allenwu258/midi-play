#include "videoexportpage.h"
#include "introtextpanel.h"
#include "introtextpreviewwidget.h"
#include "videoexportpreviewrenderer.h"
#include "app/exporttimeline.h"
#include <QCheckBox>
#include <QComboBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGridLayout>
#include <QHideEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>

namespace midi_play::presentation {
namespace {
QString formattedTime(qint64 timeUs)
{
    return QStringLiteral("%1:%2.%3")
        .arg(timeUs / 60'000'000, 2, 10, QLatin1Char('0'))
        .arg(timeUs / 1'000'000 % 60, 2, 10, QLatin1Char('0'))
        .arg(timeUs / 10'000 % 100, 2, 10, QLatin1Char('0'));
}
}

VideoExportPage::VideoExportPage(const app::VideoExportOptions& defaults,
                               qint64 previewPositionUs, QWidget* parent)
    : QWidget(parent), m_defaults(defaults), m_originalMusicPosition(previewPositionUs)
{
    setObjectName(QStringLiteral("videoExportPage"));
    auto* root = new QHBoxLayout(this);
    root->setContentsMargins(20, 20, 20, 20);
    root->setSpacing(18);
    auto* left = new QWidget(this);
    auto* leftLayout = new QVBoxLayout(left);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->setSpacing(10);
    m_preview = new IntroTextPreviewWidget(left);
    leftLayout->addWidget(m_preview, 0, Qt::AlignHCenter);
    m_previewStatus = new QLabel(left);
    m_previewStatus->setObjectName(QStringLiteral("videoPreviewStatus"));
    m_previewStatus->setWordWrap(true);
    m_previewStatus->setAlignment(Qt::AlignCenter);
    m_previewStatus->hide();
    leftLayout->addWidget(m_previewStatus);
    m_availabilityLabel = new QLabel(left);
    m_availabilityLabel->setObjectName(QStringLiteral("exportWarning"));
    m_availabilityLabel->setWordWrap(true);
    m_availabilityLabel->hide();
    leftLayout->addWidget(m_availabilityLabel);

    m_previewTransport = new QWidget(left);
    auto* transport = new QVBoxLayout(m_previewTransport);
    transport->setContentsMargins(0, 0, 0, 0);
    auto* buttons = new QGridLayout;
    auto* layoutButton = new QPushButton(QStringLiteral("布局 / 拖动"), m_previewTransport);
    layoutButton->setObjectName(QStringLiteral("introPreviewLayout"));
    m_previewPlayButton = new QPushButton(QStringLiteral("播放预览"), m_previewTransport);
    m_previewPlayButton->setObjectName(QStringLiteral("introPreviewPlay"));
    auto* start = new QPushButton(QStringLiteral("回到开头"), m_previewTransport);
    start->setObjectName(QStringLiteral("introPreviewStart"));
    m_previewMiddleButton = new QPushButton(QStringLiteral("退场中"), m_previewTransport);
    m_previewMiddleButton->setObjectName(QStringLiteral("introPreviewExit"));
    m_previewEndButton = new QPushButton(QStringLiteral("消失后"), m_previewTransport);
    m_previewEndButton->setObjectName(QStringLiteral("introPreviewHidden"));
    int buttonIndex = 0;
    for (auto* button : {layoutButton, m_previewPlayButton, start, m_previewMiddleButton, m_previewEndButton}) {
        button->setAutoDefault(false);
        buttons->addWidget(button, buttonIndex / 3, buttonIndex % 3);
        ++buttonIndex;
    }
    transport->addLayout(buttons);
    m_previewSlider = new QSlider(Qt::Horizontal, m_previewTransport);
    m_previewSlider->setObjectName(QStringLiteral("introPreviewTimeline"));
    m_previewSlider->setAccessibleName(QStringLiteral("片头预览时间轴"));
    transport->addWidget(m_previewSlider);
    m_previewTimeLabel = new QLabel(m_previewTransport);
    m_previewTimeLabel->setObjectName(QStringLiteral("introPreviewTime"));
    m_previewTimeLabel->setWordWrap(true);
    transport->addWidget(m_previewTimeLabel);
    leftLayout->addWidget(m_previewTransport);
    m_controls = new QWidget(left);
    auto* form = new QFormLayout(m_controls);
    form->setContentsMargins(0, 0, 0, 0);
    form->setHorizontalSpacing(18);
    form->setVerticalSpacing(10);

    m_size = new QComboBox(m_controls);
    m_size->addItem(QStringLiteral("1920 × 1080"), QSize(1920, 1080));
    m_size->addItem(QStringLiteral("1280 × 720"), QSize(1280, 720));
    m_size->addItem(QStringLiteral("3840 × 2160"), QSize(3840, 2160));
    form->addRow(QStringLiteral("分辨率"), m_size);

    m_retainMeasureGutter = new QCheckBox(QStringLiteral("保留小节标记栏"), m_controls);
    m_retainMeasureGutter->setObjectName(QStringLiteral("retainMeasureGutter"));
    m_retainMeasureGutter->setChecked(defaults.scene.retainMeasureGutter);
    m_retainMeasureGutter->setToolTip(QStringLiteral(
        "保留左侧小节编号及补齐琴键；取消后扩展布局并裁去标记栏。两种模式均填满 16:9 画幅。"));
    form->addRow(QStringLiteral("画幅"), m_retainMeasureGutter);

    m_fps = new QComboBox(m_controls);
    m_fps->addItem(QStringLiteral("60 FPS"), 60);
    m_fps->addItem(QStringLiteral("30 FPS"), 30);
    form->addRow(QStringLiteral("帧率"), m_fps);

    m_quality = new QComboBox(m_controls);
    m_quality->addItem(QStringLiteral("均衡"), 20);
    m_quality->addItem(QStringLiteral("高质量"), 17);
    m_quality->addItem(QStringLiteral("较小文件"), 23);
    form->addRow(QStringLiteral("画质"), m_quality);

    m_rate = new QSpinBox(m_controls);
    m_rate->setRange(20, 200);
    m_rate->setSuffix(QStringLiteral("%"));
    m_rate->setValue(defaults.ratePercent);
    form->addRow(QStringLiteral("播放速度"), m_rate);

    m_audio = new QCheckBox(QStringLiteral("包含音乐"), m_controls);
    m_audio->setChecked(defaults.includeAudio && !defaults.soundFontPath.isEmpty());
    m_audio->setEnabled(!defaults.soundFontPath.isEmpty());
    if (defaults.soundFontPath.isEmpty()) m_audio->setToolTip(QStringLiteral("请先在设置中选择音源"));
    form->addRow(QString(), m_audio);
    if (!defaults.scene.backgroundVideoPath.isEmpty()) {
        auto* originalAudio = new QLabel(defaults.backgroundVideoMuted
            ? QStringLiteral("视频原声：已屏蔽（跟随背景设置）")
            : QStringLiteral("视频原声：将包含在导出中（跟随背景设置）"), m_controls);
        originalAudio->setWordWrap(true);
        form->addRow(QString(), originalAudio);
    }

    m_metronome = new QCheckBox(QStringLiteral("包含节拍器"), m_controls);
    form->addRow(QString(), m_metronome);
    m_metronome->setChecked(defaults.includeMetronome);
    m_metronome->setEnabled(m_audio->isChecked());
    connect(m_audio, &QCheckBox::toggled, m_metronome, &QWidget::setEnabled);

    m_tail = new QSpinBox(m_controls);
    m_tail->setRange(0, 5000);
    m_tail->setSingleStep(250);
    m_tail->setSuffix(QStringLiteral(" ms"));
    m_tail->setValue(defaults.tailMilliseconds);
    form->addRow(QStringLiteral("尾音"), m_tail);

    m_introEnabled = new QCheckBox(QStringLiteral("启用入场文字"), m_controls);
    m_introEnabled->setObjectName(QStringLiteral("introTextEnabled"));
    m_introEnabled->setChecked(defaults.introText.enabled);
    form->addRow(QStringLiteral("导出内容"), m_introEnabled);

    auto* pathRow = new QWidget(m_controls);
    auto* pathLayout = new QHBoxLayout(pathRow);
    pathLayout->setContentsMargins(0, 0, 0, 0);
    m_path = new QLineEdit(defaults.outputPath, pathRow);
    m_path->setObjectName(QStringLiteral("videoExportPath"));
    auto* browse = new QPushButton(QStringLiteral("浏览…"), pathRow);
    pathLayout->addWidget(m_path, 1);
    pathLayout->addWidget(browse);
    form->addRow(QStringLiteral("输出文件"), pathRow);
    leftLayout->addWidget(m_controls);
    leftLayout->addStretch(1);
    auto* leftScroll = new QScrollArea(this);
    leftScroll->setFrameShape(QFrame::NoFrame);
    leftScroll->setWidgetResizable(true);
    leftScroll->setWidget(left);
    root->addWidget(leftScroll, 1);


    m_size->setCurrentIndex(std::max(0, m_size->findData(defaults.scene.outputSize)));
    m_fps->setCurrentIndex(std::max(0, m_fps->findData(defaults.fps)));
    m_previewFps = m_fps->currentData().toInt();
    m_quality->setCurrentIndex(std::max(0, m_quality->findData(defaults.crf)));
    m_rate->setObjectName(QStringLiteral("videoExportRate"));
    m_fps->setObjectName(QStringLiteral("videoExportFps"));
    m_tail->setObjectName(QStringLiteral("videoExportTail"));
    auto* right = new QWidget(this);
    auto* rightLayout = new QVBoxLayout(right);
    rightLayout->setContentsMargins(4, 0, 4, 0);
    m_introEditor = new IntroTextPanel(defaults.introText, right);
    rightLayout->addWidget(m_introEditor);
    m_introSummary = new QLabel(right);
    m_introSummary->setObjectName(QStringLiteral("introTextTimeSummary"));
    m_introSummary->setWordWrap(true);
    rightLayout->addWidget(m_introSummary);
    m_introError = new QLabel(right);
    m_introError->setObjectName(QStringLiteral("exportWarning"));
    m_introError->setWordWrap(true);
    rightLayout->addWidget(m_introError);
    rightLayout->addStretch();
    auto* rightScroll = new QScrollArea(this);
    rightScroll->setWidgetResizable(true);
    rightScroll->setFrameShape(QFrame::NoFrame);
    rightScroll->setWidget(right);
    rightScroll->setMinimumWidth(320);
    rightScroll->setObjectName(QStringLiteral("introTextPanel"));
    m_introPanel = rightScroll;
    root->addWidget(rightScroll, 1);

    m_renderer = new VideoExportPreviewRenderer(this);
    m_previewTimer = new QTimer(this);
    m_previewTimer->setInterval(33);
    m_textDebounce = new QTimer(this);
    m_textDebounce->setSingleShot(true);
    m_textDebounce->setInterval(150);
    connect(m_textDebounce, &QTimer::timeout, this, [this] {
        updateIntroText();
        if (!m_layoutMode) setPreviewFrame(m_previewFrame, true);
    });
    connect(m_renderer, &VideoExportPreviewRenderer::frameReady, this,
        [this](const QImage& frame, qint64 outputTimeUs) {
            m_previewStatus->hide();
            m_preview->setBaseFrame(frame, outputTimeUs);
        });
    connect(m_renderer, &VideoExportPreviewRenderer::failed, this, [this](const QString& error) {
        stopPreviewPlayback();
        m_previewStatus->setText(QStringLiteral("预览未更新：%1").arg(error));
        m_previewStatus->show();
    });
    connect(browse, &QPushButton::clicked, this, [this] {
        const QString path = QFileDialog::getSaveFileName(this, QStringLiteral("导出视频"),
            m_path->text(), QStringLiteral("MP4 视频 (*.mp4)"));
        if (!path.isEmpty()) m_path->setText(path);
    });
    connect(m_size, &QComboBox::currentIndexChanged, this, &VideoExportPage::updateScene);
    connect(m_retainMeasureGutter, &QCheckBox::toggled, this, &VideoExportPage::updateScene);
    connect(m_introEnabled, &QCheckBox::toggled, this, &VideoExportPage::updateIntroTextVisibility);
    connect(m_introEditor, &IntroTextPanel::configurationChanged, this, [this](bool layoutChanged) {
        stopPreviewPlayback();
        if (layoutChanged) enterLayoutMode();
        m_textDebounce->start();
    });
    connect(m_preview, &IntroTextPreviewWidget::positionDragged, this, [this](int x, int y) {
        m_introEditor->setPositionFromDrag(x, y);
        m_textDebounce->stop();
        updateIntroText();
    });
    const auto timelineChanged = [this] {
        stopPreviewPlayback();
        const int fps = m_fps->currentData().toInt();
        // Preserve output time, quantized to the nearest frame in the new timebase.
        m_previewFrame = (m_previewFrame * fps + m_previewFps / 2) / m_previewFps;
        m_previewFps = fps;
        updatePreviewRange();
        setPreviewFrame(m_previewFrame, true);
    };
    connect(m_rate, &QSpinBox::valueChanged, this, timelineChanged);
    connect(m_tail, &QSpinBox::valueChanged, this, timelineChanged);
    connect(m_fps, &QComboBox::currentIndexChanged, this, timelineChanged);
    connect(layoutButton, &QPushButton::clicked, this, &VideoExportPage::enterLayoutMode);
    connect(m_previewPlayButton, &QPushButton::clicked, this, [this] {
        if (m_playing) stopPreviewPlayback();
        else startPreviewPlayback();
    });
    const auto seek = [this](qint64 frame) {
        stopPreviewPlayback();
        m_layoutMode = false;
        m_preview->setLayoutMode(false);
        setPreviewFrame(frame, true);
    };
    connect(start, &QPushButton::clicked, this, [seek] { seek(0); });
    connect(m_previewMiddleButton, &QPushButton::clicked, this, [this, seek] {
        const auto config = introTextConfig();
        seek(frameAtOrAfter(config.displayMilliseconds + config.exitMilliseconds / 2));
    });
    connect(m_previewEndButton, &QPushButton::clicked, this, [this, seek] {
        const auto config = introTextConfig();
        seek(frameAtOrAfter(config.displayMilliseconds + config.exitMilliseconds));
    });
    connect(m_previewSlider, &QSlider::valueChanged, this, [seek](int frame) { seek(frame); });
    connect(m_previewTimer, &QTimer::timeout, this, [this] {
        const qint64 frame = m_playbackStartFrame + m_playbackClock.elapsed() * m_fps->currentData().toInt() / 1000;
        setPreviewFrame(frame, false);
        if (frame >= m_lastPreviewFrame) stopPreviewPlayback();
    });
    m_introPanel->setVisible(defaults.introText.enabled);
    m_previewTransport->setVisible(defaults.introText.enabled);
    updateIntroText();
    // The dialog calls setAvailability before scheduling any GPU work.
}

bool VideoExportPage::introTextEnabled() const { return m_introEnabled->isChecked(); }

app::IntroTextConfig VideoExportPage::introTextConfig() const
{
    auto config = m_introEditor->config();
    config.enabled = introTextEnabled();
    return config;
}

app::VideoExportOptions VideoExportPage::options() const
{
    auto result = m_defaults;
    result.scene.outputSize = m_size->currentData().toSize();
    result.scene.retainMeasureGutter = m_retainMeasureGutter->isChecked();
    result.fps = m_fps->currentData().toInt();
    result.crf = m_quality->currentData().toInt();
    result.ratePercent = m_rate->value();
    result.tailMilliseconds = m_tail->value();
    result.includeAudio = m_audio->isChecked();
    result.includeMetronome = result.includeAudio && m_metronome->isChecked();
    result.outputPath = outputPath();
    result.introText = introTextConfig();
    return result;
}

QString VideoExportPage::outputPath() const { return m_path->text().trimmed(); }

QString VideoExportPage::validationError() const
{
    if (!m_available) return m_unavailableReason;
    if (outputPath().isEmpty()
        || QFileInfo(outputPath()).suffix().compare(QStringLiteral("mp4"), Qt::CaseInsensitive) != 0)
        return QStringLiteral("输出文件扩展名需要为 .mp4。");
    return visualization::IntroTextAsset::validationError(introTextConfig());
}

QString VideoExportPage::previewValidationError() const
{
    if (!m_available) return m_unavailableReason.isEmpty()
        ? QStringLiteral("当前无法生成视频预览。") : m_unavailableReason;
    const app::ExportTimeline timeline(m_defaults.scene.state.durationUs,
        m_fps->currentData().toInt(), m_rate->value(), m_tail->value());
    if (!timeline.valid()) return QStringLiteral("当前乐曲没有可预览的视频时间线。");
    return visualization::IntroTextAsset::validationError(introTextConfig());
}

void VideoExportPage::setAvailability(bool available, const QString& reason)
{
    m_available = available;
    m_unavailableReason = reason;
    m_controls->setEnabled(available);
    m_introPanel->setEnabled(available);
    m_previewTransport->setEnabled(available);
    m_availabilityLabel->setText(reason);
    m_availabilityLabel->setVisible(!available && !reason.isEmpty());
    if (available) updateScene();
    else {
        stopPreviewPlayback();
        m_renderer->cancel();
    }
}

void VideoExportPage::updateScene()
{
    if (!m_available) return;
    m_renderer->setScene(options().scene);
    m_previewStatus->setText(QStringLiteral("正在生成预览…"));
    m_previewStatus->show();
    setPreviewFrame(m_previewFrame, true);
}

void VideoExportPage::updateIntroText()
{
    const QString error = m_preview->setConfig(introTextConfig());
    m_introError->setText(error);
    m_introError->setVisible(!error.isEmpty());
    m_previewPlayButton->setEnabled(previewValidationError().isEmpty());
    updatePreviewRange();
}

qint64 VideoExportPage::frameAtOrAfter(qint64 milliseconds) const
{
    return (milliseconds * m_fps->currentData().toInt() + 999) / 1000;
}

void VideoExportPage::updatePreviewRange()
{
    const auto config = introTextConfig();
    const int fps = m_fps->currentData().toInt();
    const app::ExportTimeline timeline(m_defaults.scene.state.durationUs, fps, m_rate->value(), m_tail->value());
    const qint64 exitEndFrame = frameAtOrAfter(config.displayMilliseconds + config.exitMilliseconds);
    const qint64 lastVideoFrame = timeline.valid() ? timeline.frameCount() - 1 : 0;
    m_lastPreviewFrame = std::min(lastVideoFrame, exitEndFrame + (fps + 1) / 2);
    const QSignalBlocker blocker(m_previewSlider);
    m_previewSlider->setRange(0, int(m_lastPreviewFrame));
    m_previewFrame = std::min(m_previewFrame, m_lastPreviewFrame);
    m_previewSlider->setValue(int(m_previewFrame));
    m_previewTimeLabel->setText(QStringLiteral("%1 / %2 · %3 · 无声预览")
        .arg(formattedTime(m_previewFrame * 1'000'000 / fps),
             formattedTime(m_lastPreviewFrame * 1'000'000 / fps),
             m_layoutMode ? QStringLiteral("布局") : QStringLiteral("时间")));
    m_previewMiddleButton->setEnabled(frameAtOrAfter(config.displayMilliseconds + config.exitMilliseconds / 2) <= lastVideoFrame);
    m_previewEndButton->setEnabled(exitEndFrame <= lastVideoFrame);
    const QString unavailable = QStringLiteral("该时点超出成品视频的最后一帧。");
    m_previewMiddleButton->setToolTip(m_previewMiddleButton->isEnabled() ? QString() : unavailable);
    m_previewEndButton->setToolTip(m_previewEndButton->isEnabled() ? QString() : unavailable);
    QString summary = QStringLiteral("从视频开头显示，%1 秒开始退场，%2 秒完全消失。")
        .arg(config.displayMilliseconds / 1000.0, 0, 'f', 2)
        .arg((config.displayMilliseconds + config.exitMilliseconds) / 1000.0, 0, 'f', 2);
    if (timeline.valid() && exitEndFrame > lastVideoFrame)
        summary += QStringLiteral("\n视频将在 %1 秒结束，退场动画会被截断。")
            .arg(timeline.videoDurationUs() / 1'000'000.0, 0, 'f', 2);
    m_introSummary->setText(summary);
}

void VideoExportPage::updateIntroTextVisibility(bool enabled)
{
    stopPreviewPlayback();
    m_introPanel->setVisible(enabled && m_pageActive);
    m_previewTransport->setVisible(enabled);
    m_layoutMode = true;
    m_preview->setLayoutMode(true);
    updateIntroText();
    setPreviewFrame(0, true);
    emit introTextPanelVisibilityChanged(enabled);
}

void VideoExportPage::enterLayoutMode()
{
    stopPreviewPlayback();
    const bool needsFrame = !m_layoutMode || m_previewFrame != 0;
    m_layoutMode = true;
    m_preview->setLayoutMode(true);
    if (needsFrame) setPreviewFrame(0, true);
    else updatePreviewRange();
}

void VideoExportPage::startPreviewPlayback()
{
    if (!introTextEnabled() || !previewValidationError().isEmpty()) return;
    m_textDebounce->stop();
    updateIntroText();
    m_layoutMode = false;
    m_preview->setLayoutMode(false);
    if (m_previewFrame >= m_lastPreviewFrame) setPreviewFrame(0, true);
    m_playbackStartFrame = m_previewFrame;
    m_playbackClock.start();
    m_playing = true;
    m_previewPlayButton->setText(QStringLiteral("暂停"));
    m_previewTimer->start();
}

void VideoExportPage::stopPreviewPlayback()
{
    m_previewTimer->stop();
    m_playing = false;
    m_previewPlayButton->setText(QStringLiteral("播放预览"));
}

void VideoExportPage::setPreviewFrame(qint64 frame, bool discontinuity)
{
    m_previewFrame = std::clamp<qint64>(frame, 0, m_lastPreviewFrame);
    const int fps = m_fps->currentData().toInt();
    const qint64 outputUs = m_previewFrame * 1'000'000 / fps;
    const app::ExportTimeline timeline(m_defaults.scene.state.durationUs, fps, m_rate->value(), m_tail->value());
    const qint64 musicUs = introTextEnabled() ? timeline.musicPositionUs(m_previewFrame) : m_originalMusicPosition;
    {
        const QSignalBlocker blocker(m_previewSlider);
        m_previewSlider->setValue(int(m_previewFrame));
    }
    m_previewTimeLabel->setText(QStringLiteral("%1 / %2 · %3 · 无声预览")
        .arg(formattedTime(outputUs), formattedTime(m_lastPreviewFrame * 1'000'000 / fps),
             m_layoutMode ? QStringLiteral("布局") : QStringLiteral("时间")));
    if (m_available) m_renderer->request(outputUs, musicUs, discontinuity);
}

void VideoExportPage::hideEvent(QHideEvent* event)
{
    stopPreviewPlayback();
    QWidget::hideEvent(event);
}

void VideoExportPage::setPageActive(bool active)
{
    m_pageActive = active;
    m_introPanel->setVisible(active && introTextEnabled());
    if (!active) stopPreviewPlayback();
}
} // namespace midi_play::presentation
