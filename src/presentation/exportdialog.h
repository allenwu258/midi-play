#pragma once

// 导出配置对话框：组合音频、视频参数页，统一处理校验与覆盖确认。

#include "app/audioexportservice.h"
#include "app/videoexportservice.h"

#include <QDialog>

class QDialogButtonBox;
class QTabWidget;

namespace midi_play::presentation {
class AudioExportPage;
class VideoExportPage;

// 接受对话框仅表示配置已确认；调用方读取选项后启动应用层导出任务。
class ExportDialog final : public QDialog {
public:
    // 当前标签页决定导出类型。
    enum class ExportType { Audio, Video };

    // 以当前乐曲、音源和场景快照初始化表单，并传入视频能力检测结果。
    ExportDialog(const QString& sourcePath, const QString& soundFontPath,
                 const app::VideoExportOptions& videoDefaults, qint64 previewPositionUs,
                 bool videoAvailable, const QString& videoUnavailableReason,
                 QWidget* parent = nullptr);

    // 返回当前导出类型及各页的选项快照。
    ExportType exportType() const noexcept;
    app::AudioExportOptions audioOptions() const;
    app::VideoExportOptions videoOptions() const;

protected:
    void showEvent(QShowEvent* event) override;

private:
    // 当前页校验通过且用户允许覆盖后，结束模态配置流程。
    void acceptExport();
    void updateVideoLayout();
    int preferredHeight() const;

    QTabWidget* m_tabs = nullptr;
    AudioExportPage* m_audioPage = nullptr;
    VideoExportPage* m_videoPage = nullptr;
    QDialogButtonBox* m_buttons = nullptr;
    QSize m_compactSize {680, 920};
    bool m_expanded = false;
    bool m_initialSizeApplied = false;
};
} // namespace midi_play::presentation
