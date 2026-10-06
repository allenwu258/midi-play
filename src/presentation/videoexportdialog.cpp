#include "videoexportdialog.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QFutureWatcher>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>
#include <QtConcurrent>

namespace midi_play::presentation {
VideoExportDialog::VideoExportDialog(const app::VideoExportOptions& defaults, qint64 previewPositionUs, QWidget* parent)
    : QDialog(parent), m_defaults(defaults), m_previewPosition(previewPositionUs)
{
    setWindowTitle(QStringLiteral("导出视频"));
    setMinimumWidth(560);
    auto* root = new QVBoxLayout(this);
    m_preview = new QLabel(this);
    m_preview->setFixedSize(512, 288);
    m_preview->setAlignment(Qt::AlignCenter);
    root->addWidget(m_preview, 0, Qt::AlignCenter);
    auto* form = new QFormLayout(); root->addLayout(form);
    m_size = new QComboBox(this);
    m_size->addItem(QStringLiteral("1920 × 1080"), QSize(1920, 1080));
    m_size->addItem(QStringLiteral("1280 × 720"), QSize(1280, 720));
    m_size->addItem(QStringLiteral("3840 × 2160"), QSize(3840, 2160));
    form->addRow(QStringLiteral("分辨率"), m_size);
    m_fps = new QComboBox(this); m_fps->addItem(QStringLiteral("60 FPS"), 60); m_fps->addItem(QStringLiteral("30 FPS"), 30);
    form->addRow(QStringLiteral("帧率"), m_fps);
    m_quality = new QComboBox(this);
    m_quality->addItem(QStringLiteral("均衡"), 20); m_quality->addItem(QStringLiteral("高质量"), 17); m_quality->addItem(QStringLiteral("较小文件"), 23);
    form->addRow(QStringLiteral("画质"), m_quality);
    m_rate = new QSpinBox(this); m_rate->setRange(20, 200); m_rate->setSuffix(QStringLiteral("%")); m_rate->setValue(defaults.ratePercent);
    form->addRow(QStringLiteral("播放速度"), m_rate);
    m_audio = new QCheckBox(QStringLiteral("包含音乐"), this); m_audio->setChecked(!defaults.soundFontPath.isEmpty());
    m_audio->setEnabled(!defaults.soundFontPath.isEmpty());
    if (defaults.soundFontPath.isEmpty()) m_audio->setToolTip(QStringLiteral("请先在设置中选择音源"));
    form->addRow(QString(), m_audio);
    m_metronome = new QCheckBox(QStringLiteral("包含节拍器"), this);
    form->addRow(QString(), m_metronome);
    m_metronome->setEnabled(m_audio->isChecked());
    connect(m_audio, &QCheckBox::toggled, m_metronome, &QWidget::setEnabled);
    m_tail = new QSpinBox(this); m_tail->setRange(0, 5000); m_tail->setSingleStep(250);
    m_tail->setSuffix(QStringLiteral(" ms")); m_tail->setValue(500);
    form->addRow(QStringLiteral("尾音"), m_tail);
    auto* pathRow = new QWidget(this); auto* pathLayout = new QHBoxLayout(pathRow); pathLayout->setContentsMargins(0, 0, 0, 0);
    m_path = new QLineEdit(defaults.outputPath, pathRow);
    auto* browse = new QPushButton(QStringLiteral("浏览"), pathRow);
    pathLayout->addWidget(m_path, 1); pathLayout->addWidget(browse);
    form->addRow(QStringLiteral("输出文件"), pathRow);
    connect(browse, &QPushButton::clicked, this, [this] {
        const auto path = QFileDialog::getSaveFileName(this, QStringLiteral("导出视频"), m_path->text(), QStringLiteral("MP4 视频 (*.mp4)"));
        if (!path.isEmpty()) m_path->setText(path);
    });
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("开始导出")); root->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_size, &QComboBox::currentIndexChanged, this, [this] { updatePreview(); });
    updatePreview();
}
VideoExportDialog::~VideoExportDialog() { if (m_previewCancel) m_previewCancel->store(true); }
app::VideoExportOptions VideoExportDialog::options() const
{
    auto result = m_defaults;
    result.scene.outputSize = m_size->currentData().toSize(); result.fps = m_fps->currentData().toInt();
    result.crf = m_quality->currentData().toInt(); result.ratePercent = m_rate->value(); result.tailMilliseconds = m_tail->value();
    result.includeAudio = m_audio->isChecked(); result.includeMetronome = result.includeAudio && m_metronome->isChecked();
    result.outputPath = m_path->text().trimmed();
    return result;
}
void VideoExportDialog::updatePreview()
{
#if MIDI_PLAY_HAS_VULKAN
    if (m_previewCancel) m_previewCancel->store(true);
    m_previewCancel = std::make_shared<std::atomic_bool>(false);
    const auto cancel = m_previewCancel;
    const auto revision = ++m_previewRevision;
    m_preview->setText(QStringLiteral("正在生成预览"));
    auto config = options().scene;
    config.outputSize = {1024, 576};
    const qint64 position = m_previewPosition;
    auto* watcher = new QFutureWatcher<QPair<QImage, QString>>(this);
    connect(watcher, &QFutureWatcher<QPair<QImage, QString>>::finished, this, [this, watcher, revision] {
        const auto result = watcher->result(); watcher->deleteLater();
        if (revision != m_previewRevision) return;
        if (result.first.isNull()) m_preview->setText(result.second);
        else m_preview->setPixmap(QPixmap::fromImage(result.first).scaled(m_preview->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
    });
    watcher->setFuture(QtConcurrent::run([config, cancel, position] {
        visualization::VulkanOffscreenRenderer renderer; QImage frame; QString error;
        if (renderer.initialize(config, &error)) renderer.render(position, frame, cancel.get(), &error);
        return qMakePair(frame, error);
    }));
#endif
}
} // namespace midi_play::presentation
