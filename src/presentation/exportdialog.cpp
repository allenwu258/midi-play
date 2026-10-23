// 导出对话框实现：将页面参数校验与文件覆盖确认串联为接受流程。
#include "exportdialog.h"

#include "audioexportpage.h"
#include "videoexportpage.h"

#include <QDialogButtonBox>
#include <QFileInfo>
#include <QMessageBox>
#include <QPushButton>
#include <QScreen>
#include <QTabBar>
#include <QTabWidget>
#include <QVBoxLayout>
#include <algorithm>

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

    // 两个页面分别保存配置；视频不可用时保留页面并展示具体原因。
    m_tabs = new QTabWidget(this);
    m_tabs->setObjectName(QStringLiteral("exportTabs"));
    m_tabs->tabBar()->setObjectName(QStringLiteral("exportTabBar"));
    m_audioPage = new AudioExportPage(sourcePath, soundFontPath, m_tabs);
    m_videoPage = new VideoExportPage(videoDefaults, previewPositionUs, m_tabs);
    m_tabs->addTab(m_audioPage, QStringLiteral("音频"));
    m_tabs->addTab(m_videoPage, QStringLiteral("视频"));
    m_videoPage->setAvailability(videoAvailable, videoUnavailableReason);
    root->addWidget(m_tabs, 1);
    connect(m_videoPage, &VideoExportPage::introTextPanelVisibilityChanged,
            this, &ExportDialog::updateVideoLayout);
    connect(m_tabs, &QTabWidget::currentChanged, this, &ExportDialog::updateVideoLayout);

    m_buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    m_buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("开始导出"));
    root->addWidget(m_buttons);
    connect(m_buttons, &QDialogButtonBox::accepted, this, &ExportDialog::acceptExport);
    connect(m_buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    updateVideoLayout();
}

void ExportDialog::updateVideoLayout()
{
    const bool video = exportType() == ExportType::Video;
    m_videoPage->setPageActive(video);
    const bool expanded = video && m_videoPage->introTextEnabled();
    if (expanded == m_expanded) return;
    if (expanded) m_compactSize = size();
    m_expanded = expanded;
    layout()->activate();
    const QRect available = screen()->availableGeometry();
    const QSize wanted(expanded ? std::max(1150, m_compactSize.width()) : m_compactSize.width(),
                       m_compactSize.height());
    resize(wanted.boundedTo(available.size() - QSize(24, 48)));
    if (isVisible()) {
        move(std::clamp(x(), available.left(), std::max(available.left(), available.right() - width())),
             std::clamp(y(), available.top(), std::max(available.top(), available.bottom() - height() - 32)));
    }
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
    // 仅校验当前页，避免另一种导出格式的配置阻塞本次操作。
    const bool video = exportType() == ExportType::Video;
    const QString error = video ? m_videoPage->validationError() : m_audioPage->validationError();
    if (!error.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("导出"), error);
        return;
    }
    // 在启动后台任务前完成覆盖确认；最终文件写入仍由导出服务负责。
    const QString path = video ? m_videoPage->outputPath() : m_audioPage->outputPath();
    if (QFileInfo::exists(path)
        && QMessageBox::question(this, QStringLiteral("覆盖文件"),
            QStringLiteral("目标文件已存在，确定覆盖吗？\n%1").arg(path)) != QMessageBox::Yes) {
        return;
    }
    accept();
}

} // namespace midi_play::presentation
