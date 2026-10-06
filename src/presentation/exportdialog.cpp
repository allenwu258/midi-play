#include "exportdialog.h"

#include "audioexportpage.h"
#include "videoexportpage.h"

#include <QDialogButtonBox>
#include <QFileInfo>
#include <QMessageBox>
#include <QPushButton>
#include <QTabBar>
#include <QTabWidget>
#include <QVBoxLayout>

namespace midi_play::presentation {

ExportDialog::ExportDialog(const QString& sourcePath, const QString& soundFontPath,
                           const app::VideoExportOptions& videoDefaults,
                           qint64 previewPositionUs, bool videoAvailable,
                           const QString& videoUnavailableReason, QWidget* parent)
    : QDialog(parent)
{
    setObjectName(QStringLiteral("exportDialog"));
    setWindowTitle(QStringLiteral("导出"));
    setMinimumSize(620, 620);
    resize(680, 760);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(12, 12, 12, 12);
    root->setSpacing(12);

    m_tabs = new QTabWidget(this);
    m_tabs->setObjectName(QStringLiteral("exportTabs"));
    m_tabs->tabBar()->setObjectName(QStringLiteral("exportTabBar"));
    m_audioPage = new AudioExportPage(sourcePath, soundFontPath, m_tabs);
    m_videoPage = new VideoExportPage(videoDefaults, previewPositionUs, m_tabs);
    m_tabs->addTab(m_audioPage, QStringLiteral("音频"));
    m_tabs->addTab(m_videoPage, QStringLiteral("视频"));
    m_videoPage->setAvailability(videoAvailable, videoUnavailableReason);
    root->addWidget(m_tabs, 1);

    m_buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    m_buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("开始导出"));
    root->addWidget(m_buttons);
    connect(m_buttons, &QDialogButtonBox::accepted, this, &ExportDialog::acceptExport);
    connect(m_buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

ExportDialog::ExportType ExportDialog::exportType() const noexcept
{
    return m_tabs && m_tabs->currentIndex() == 1 ? ExportType::Video : ExportType::Audio;
}

app::AudioExportOptions ExportDialog::audioOptions() const
{
    return m_audioPage->options();
}

app::VideoExportOptions ExportDialog::videoOptions() const
{
    return m_videoPage->options();
}

void ExportDialog::acceptExport()
{
    const bool video = exportType() == ExportType::Video;
    const QString error = video ? m_videoPage->validationError() : m_audioPage->validationError();
    if (!error.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("导出"), error);
        return;
    }
    const QString path = video ? m_videoPage->outputPath() : m_audioPage->outputPath();
    if (QFileInfo::exists(path)
        && QMessageBox::question(this, QStringLiteral("覆盖文件"),
            QStringLiteral("目标文件已存在，确定覆盖吗？\n%1").arg(path)) != QMessageBox::Yes) {
        return;
    }
    accept();
}

} // namespace midi_play::presentation
