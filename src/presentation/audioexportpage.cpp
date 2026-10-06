#include "audioexportpage.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>

namespace midi_play::presentation {

AudioExportPage::AudioExportPage(const QString& sourcePath, const QString& soundFontPath,
                                 QWidget* parent)
    : QWidget(parent), m_soundFontPath(soundFontPath)
{
    setObjectName(QStringLiteral("audioExportPage"));
    auto* form = new QFormLayout(this);
    form->setContentsMargins(20, 20, 20, 20);
    form->setHorizontalSpacing(18);
    form->setVerticalSpacing(12);

    auto* song = new QLabel(QFileInfo(sourcePath).fileName(), this);
    song->setObjectName(QStringLiteral("exportMeta"));
    song->setToolTip(sourcePath);
    form->addRow(QStringLiteral("乐曲"), song);

    auto* font = new QLabel(soundFontPath.isEmpty() ? QStringLiteral("未配置音源")
                                                   : QFileInfo(soundFontPath).fileName(), this);
    font->setObjectName(soundFontPath.isEmpty() ? QStringLiteral("exportWarning")
                                                : QStringLiteral("exportMeta"));
    font->setToolTip(soundFontPath);
    form->addRow(QStringLiteral("音源"), font);
    if (soundFontPath.isEmpty()) {
        auto* warning = new QLabel(QStringLiteral("请先在设置中选择 SF2/SF3 音源。"), this);
        warning->setObjectName(QStringLiteral("exportWarning"));
        warning->setWordWrap(true);
        form->addRow(QString(), warning);
    }

    m_format = new QComboBox(this);
    m_format->addItem(QStringLiteral("MP3"));
    m_format->addItem(QStringLiteral("WAV (16-bit PCM)"));
    form->addRow(QStringLiteral("格式"), m_format);

    m_bitrate = new QComboBox(this);
    for (int value : {128, 160, 192, 256, 320})
        m_bitrate->addItem(QStringLiteral("%1 kbps").arg(value), value);
    m_bitrate->setCurrentIndex(2);
    form->addRow(QStringLiteral("MP3 码率"), m_bitrate);
    connect(m_format, &QComboBox::currentIndexChanged, m_bitrate,
            [this](int index) { m_bitrate->setEnabled(index == 0); });

    m_sampleRate = new QComboBox(this);
    m_sampleRate->addItem(QStringLiteral("44.1 kHz"), 44100);
    m_sampleRate->addItem(QStringLiteral("48 kHz"), 48000);
    form->addRow(QStringLiteral("采样率"), m_sampleRate);

    m_metronome = new QCheckBox(QStringLiteral("包含节拍器"), this);
    form->addRow(QString(), m_metronome);

    m_tail = new QSpinBox(this);
    m_tail->setRange(0, 5000);
    m_tail->setSingleStep(250);
    m_tail->setSuffix(QStringLiteral(" ms"));
    m_tail->setValue(500);
    form->addRow(QStringLiteral("尾音"), m_tail);

    auto* pathRow = new QWidget(this);
    auto* pathLayout = new QHBoxLayout(pathRow);
    pathLayout->setContentsMargins(0, 0, 0, 0);
    auto* path = new QLineEdit(pathRow);
    path->setObjectName(QStringLiteral("audioExportPath"));
    path->setText(QFileInfo(sourcePath).absolutePath() + QLatin1Char('/')
                  + QFileInfo(sourcePath).completeBaseName() + QStringLiteral(".mp3"));
    m_path = path;
    auto* browse = new QPushButton(QStringLiteral("浏览…"), pathRow);
    pathLayout->addWidget(path, 1);
    pathLayout->addWidget(browse);
    form->addRow(QStringLiteral("输出文件"), pathRow);

    connect(m_format, &QComboBox::currentIndexChanged, this, [this](int index) {
        const QFileInfo current(m_path->text());
        if (current.suffix().compare(QStringLiteral("mp3"), Qt::CaseInsensitive) == 0
            || current.suffix().compare(QStringLiteral("wav"), Qt::CaseInsensitive) == 0) {
            m_path->setText(current.absolutePath() + QLatin1Char('/') + current.completeBaseName()
                            + (index == 0 ? QStringLiteral(".mp3") : QStringLiteral(".wav")));
        }
    });
    connect(browse, &QPushButton::clicked, this, [this] {
        const QString selected = QFileDialog::getSaveFileName(
            this, QStringLiteral("导出音频"), m_path->text(),
            m_format->currentIndex() == 0 ? QStringLiteral("MP3 音频 (*.mp3)")
                                           : QStringLiteral("WAV 音频 (*.wav)"));
        if (!selected.isEmpty()) m_path->setText(selected);
    });
}

app::AudioExportOptions AudioExportPage::options() const
{
    app::AudioExportOptions result;
    result.format = m_format->currentIndex() == 0
        ? encoding::AudioFileFormat::Mp3 : encoding::AudioFileFormat::Wav;
    result.sampleRate = m_sampleRate->currentData().toInt();
    result.bitrateKbps = m_bitrate->currentData().toInt();
    result.includeMetronome = m_metronome->isChecked();
    result.tailMilliseconds = m_tail->value();
    result.soundFontPath = m_soundFontPath;
    result.outputPath = QFileInfo(outputPath()).absoluteFilePath();
    return result;
}

QString AudioExportPage::outputPath() const
{
    return m_path ? m_path->text().trimmed() : QString();
}

QString AudioExportPage::validationError() const
{
    if (!hasSoundFont()) return QStringLiteral("请先在设置中选择 SF2/SF3 音源。");
    const auto selected = options();
    const QString expectedSuffix = selected.format == encoding::AudioFileFormat::Mp3
        ? QStringLiteral("mp3") : QStringLiteral("wav");
    if (outputPath().isEmpty()
        || QFileInfo(selected.outputPath).suffix().compare(expectedSuffix, Qt::CaseInsensitive) != 0) {
        return QStringLiteral("输出文件扩展名需要与所选格式一致。");
    }
    return {};
}

} // namespace midi_play::presentation
