#include "mainwindow.h"

#include "app/playerapplicationservice.h"

#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSlider>
#include <QVBoxLayout>
#include <QWidget>

#include <limits>

namespace midi_play::presentation {

MainWindow::MainWindow(app::PlayerApplicationService* service, QWidget* parent)
    : QMainWindow(parent), m_service(service)
{
    setWindowTitle(QStringLiteral("MIDI Play"));
    resize(720, 260);
    auto* central = new QWidget(this);
    auto* root = new QVBoxLayout(central);
    auto* files = new QFormLayout();
    m_fileLabel = new QLabel(QStringLiteral("未加载 MusicXML"));
    m_soundFontLabel = new QLabel(QStringLiteral("未加载 SF2"));
    files->addRow(QStringLiteral("曲目"), m_fileLabel);
    files->addRow(QStringLiteral("音源"), m_soundFontLabel);
    root->addLayout(files);

    auto* fileButtons = new QHBoxLayout();
    auto* openMusic = new QPushButton(QStringLiteral("打开 MusicXML"));
    auto* openSf2 = new QPushButton(QStringLiteral("加载 SF2"));
    fileButtons->addWidget(openMusic);
    fileButtons->addWidget(openSf2);
    fileButtons->addStretch();
    root->addLayout(fileButtons);

    m_positionSlider = new QSlider(Qt::Horizontal);
    m_positionSlider->setRange(0, 0);
    root->addWidget(m_positionSlider);
    m_timeLabel = new QLabel(QStringLiteral("00:00 / 00:00"));
    root->addWidget(m_timeLabel);

    auto* controls = new QHBoxLayout();
    m_playButton = new QPushButton(QStringLiteral("播放"));
    m_pauseButton = new QPushButton(QStringLiteral("暂停"));
    m_stopButton = new QPushButton(QStringLiteral("停止"));
    controls->addWidget(m_playButton);
    controls->addWidget(m_pauseButton);
    controls->addWidget(m_stopButton);
    controls->addStretch();
    root->addLayout(controls);
    m_statusLabel = new QLabel(QStringLiteral("就绪"));
    root->addWidget(m_statusLabel);
    setCentralWidget(central);

    connect(openMusic, &QPushButton::clicked, this, &MainWindow::openMusicXml);
    connect(openSf2, &QPushButton::clicked, this, &MainWindow::openSoundFont);
    connect(m_playButton, &QPushButton::clicked, m_service, &app::PlayerApplicationService::play);
    connect(m_pauseButton, &QPushButton::clicked, m_service, &app::PlayerApplicationService::pause);
    connect(m_stopButton, &QPushButton::clicked, m_service, &app::PlayerApplicationService::stop);
    connect(m_positionSlider, &QSlider::sliderPressed, this, [this] { m_sliderDragging = true; });
    connect(m_positionSlider, &QSlider::sliderReleased, this, [this] {
        m_sliderDragging = false;
        m_service->seek(m_positionSlider->value());
    });
    connect(m_service, &app::PlayerApplicationService::documentLoaded, this, [this](const QString& title, qint64 duration) {
        m_fileLabel->setText(title);
        m_positionSlider->setRange(0, static_cast<int>(std::min<qint64>(duration, std::numeric_limits<int>::max())));
        m_statusLabel->setText(QStringLiteral("曲目已加载"));
    });
    connect(m_service, &app::PlayerApplicationService::positionChanged, this, &MainWindow::updatePosition);
    connect(m_service, &app::PlayerApplicationService::soundFontLoaded, this, [this](const QString& path) {
        m_soundFontLabel->setText(path);
        m_statusLabel->setText(QStringLiteral("SF2 已加载"));
    });
    connect(m_service, &app::PlayerApplicationService::busyChanged, this, [this](bool busy) {
        m_statusLabel->setText(busy ? QStringLiteral("处理中...") : QStringLiteral("就绪"));
    });
    connect(m_service, &app::PlayerApplicationService::errorOccurred, this, [this](const QString& message) {
        m_statusLabel->setText(message);
        QMessageBox::warning(this, QStringLiteral("播放器错误"), message);
    });
}

void MainWindow::openMusicXml()
{
    const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("打开 MusicXML"), {},
                                                       QStringLiteral("MusicXML (*.xml *.musicxml);;所有文件 (*.*)"));
    if (!path.isEmpty()) m_service->openMusicXml(path);
}

void MainWindow::openSoundFont()
{
    const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("加载 SF2"), {},
                                                       QStringLiteral("SoundFont (*.sf2 *.sf3);;所有文件 (*.*)"));
    if (!path.isEmpty()) m_service->loadSoundFont(path);
}

void MainWindow::updatePosition(qint64 position, qint64 duration)
{
    if (!m_sliderDragging) m_positionSlider->setValue(static_cast<int>(position));
    auto format = [](qint64 us) { const qint64 sec = us / 1'000'000; return QStringLiteral("%1:%2").arg(sec / 60, 2, 10, QLatin1Char('0')).arg(sec % 60, 2, 10, QLatin1Char('0')); };
    m_timeLabel->setText(QStringLiteral("%1 / %2").arg(format(position), format(duration)));
}

} // namespace midi_play::presentation
