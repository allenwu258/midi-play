#include "mainwindow.h"

#include "app/playerapplicationservice.h"
#include "presentation/visualization/fallingnotesview.h"

#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QSlider>
#include <QStyle>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidget>

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

} // namespace

MainWindow::MainWindow(app::PlayerApplicationService* service, QWidget* parent)
    : QMainWindow(parent), m_service(service)
{
    setWindowTitle(QStringLiteral("MIDI Play"));
    resize(1180, 760);
    setMinimumSize(720, 540);

    auto* central = new QWidget(this);
    central->setObjectName(QStringLiteral("applicationRoot"));
    auto* root = new QVBoxLayout(central);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    auto* topBar = new QWidget(central);
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
    m_fileLabel->setMinimumWidth(160);
    m_fileLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    topLayout->addWidget(m_fileLabel, 1);
    m_soundFontLabel = new QLabel(QStringLiteral("SF2 未加载"), topBar);
    m_soundFontLabel->setObjectName(QStringLiteral("soundFontLabel"));
    topLayout->addWidget(m_soundFontLabel);

    auto* openMusic = toolButton(topBar, style()->standardIcon(QStyle::SP_DialogOpenButton),
                                 QStringLiteral("打开乐曲"), QStringLiteral("打开 MusicXML 或 MIDI 文件"));
    auto* openSf2 = toolButton(topBar, style()->standardIcon(QStyle::SP_DriveHDIcon),
                               QStringLiteral("加载 SF2"), QStringLiteral("加载 SoundFont 音源"));
    topLayout->addWidget(openMusic);
    topLayout->addWidget(openSf2);
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
    controlRow->addWidget(verticalSeparator(transport));
    m_timeLabel = new QLabel(QStringLiteral("00:00 / 00:00"), transport);
    m_timeLabel->setObjectName(QStringLiteral("timeLabel"));
    m_timeLabel->setMinimumWidth(118);
    controlRow->addWidget(m_timeLabel);
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
        QLabel#timeLabel { color: #e6e7e2; font-family: Consolas, monospace; font-size: 11px; }
        QFrame#toolbarSeparator { color: #3a3d40; max-height: 26px; }
        QToolButton { color: #dfe1dc; border: 1px solid transparent; padding: 6px 8px; }
        QToolButton:hover { background: #292c2f; border-color: #3a3e41; }
        QToolButton:pressed { background: #34383b; }
        QToolButton:disabled { color: #676c68; }
        QSlider::groove:horizontal { height: 4px; background: #393d3f; }
        QSlider::sub-page:horizontal { background: #f4d35e; }
        QSlider::handle:horizontal { width: 14px; margin: -5px 0; border-radius: 7px; background: #f0f1ed; }
        QSlider::handle:horizontal:hover { background: #f4d35e; }
    )"));

    connect(openMusic, &QToolButton::clicked, this, &MainWindow::openMusicFile);
    connect(openSf2, &QToolButton::clicked, this, &MainWindow::openSoundFont);
    connect(m_playButton, &QToolButton::clicked, m_service, &app::PlayerApplicationService::play);
    connect(m_pauseButton, &QToolButton::clicked, m_service, &app::PlayerApplicationService::pause);
    connect(m_stopButton, &QToolButton::clicked, m_service, &app::PlayerApplicationService::stop);
    connect(m_positionSlider, &QSlider::sliderPressed, this, [this] { m_sliderDragging = true; });
    connect(m_positionSlider, &QSlider::sliderMoved, this, [this](int value) {
        if (m_durationUs <= 0) return;
        const qint64 previewUs = static_cast<qint64>(
            static_cast<long double>(value) * m_durationUs / kSliderResolution);
        m_timeLabel->setText(QStringLiteral("%1 / %2").arg(formatTime(previewUs), formatTime(m_durationUs)));
    });
    connect(m_positionSlider, &QSlider::sliderReleased, this, [this] {
        if (m_durationUs > 0) {
            const qint64 targetUs = static_cast<qint64>(
                static_cast<long double>(m_positionSlider->value()) * m_durationUs / kSliderResolution);
            m_service->seek(targetUs);
        }
        m_sliderDragging = false;
    });

    connect(m_service, &app::PlayerApplicationService::documentLoaded, this,
            [this](const QString& title, qint64 duration) {
                m_fileLabel->setText(title);
                m_fileLabel->setToolTip(m_service->fileName());
                m_durationUs = duration;
                m_positionSlider->setEnabled(duration > 0);
                m_statusLabel->setText(QStringLiteral("曲目已加载"));
                updateTransportControls();
            });
    connect(m_service, &app::PlayerApplicationService::visualizationReady,
            m_visualization, &visualization::FallingNotesView::setChart);
    connect(m_service, &app::PlayerApplicationService::positionChanged,
            this, &MainWindow::updatePosition);
    connect(m_service, &app::PlayerApplicationService::playbackStateChanged,
            this, &MainWindow::updatePlaybackState);
    connect(m_service, &app::PlayerApplicationService::soundFontLoaded, this, [this](const QString& path) {
        const QFileInfo file(path);
        m_soundFontLabel->setText(file.fileName().isEmpty() ? QStringLiteral("SF2 已配置") : file.fileName());
        m_soundFontLabel->setToolTip(path);
        m_statusLabel->setText(QStringLiteral("SoundFont 已加载"));
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

    updateTransportControls();
}

void MainWindow::openMusicFile()
{
    const QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("打开音乐文件"), {},
        QStringLiteral("音乐文件 (*.xml *.musicxml *.mid *.midi *.kar);;MusicXML (*.xml *.musicxml);;MIDI (*.mid *.midi *.kar);;所有文件 (*.*)"));
    if (!path.isEmpty()) m_service->openFile(path);
}

void MainWindow::openSoundFont()
{
    const QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("加载 SoundFont"), {},
        QStringLiteral("SoundFont (*.sf2 *.sf3);;所有文件 (*.*)"));
    if (!path.isEmpty()) m_service->loadSoundFont(path);
}

void MainWindow::updatePosition(qint64 position, qint64 duration)
{
    m_positionUs = std::clamp<qint64>(position, 0, std::max<qint64>(0, duration));
    m_durationUs = std::max<qint64>(0, duration);
    if (!m_sliderDragging) {
        const int sliderValue = m_durationUs > 0
            ? static_cast<int>(static_cast<long double>(m_positionUs) * kSliderResolution / m_durationUs)
            : 0;
        m_positionSlider->setValue(std::clamp(sliderValue, 0, kSliderResolution));
        m_timeLabel->setText(QStringLiteral("%1 / %2").arg(formatTime(m_positionUs), formatTime(m_durationUs)));
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
