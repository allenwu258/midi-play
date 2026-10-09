#pragma once

// 视频导出参数页：编辑场景导出选项，并异步生成当前音乐位置的预览。

#include "app/videoexportservice.h"

#include <QWidget>

#include <atomic>
#include <memory>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QSpinBox;
class QWidget;

namespace midi_play::presentation {

// 保留输入场景快照；预览取消标志随任务共享，不依赖页面的生存期。
class VideoExportPage final : public QWidget {
public:
    // 从默认参数初始化控件，预览位置固定为打开对话框时的音乐位置。
    explicit VideoExportPage(const app::VideoExportOptions& defaults,
                             qint64 previewPositionUs, QWidget* parent = nullptr);
    // 请求取消预览，不在 GUI 线程同步等待后台任务。
    ~VideoExportPage() override;

    // 在默认快照上覆盖可编辑字段，保留图表及其他场景设置。
    app::VideoExportOptions options() const;
    // 返回去除首尾空白的输出路径。
    QString outputPath() const;
    // 同步表单可用性、错误说明及预览任务状态。
    void setAvailability(bool available, const QString& reason);
    bool isAvailable() const noexcept { return m_available; }
    // 检查视频能力及 MP4 扩展名；空字符串表示通过。
    QString validationError() const;

private:
    // 取消旧请求并创建新预览；结果仅在版本仍匹配时写回。
    void updatePreview();

    // 参数快照独立于外部表单，后台任务按值捕获所需场景。
    app::VideoExportOptions m_defaults;
    QComboBox* m_size = nullptr;
    QComboBox* m_fps = nullptr;
    QComboBox* m_quality = nullptr;
    QSpinBox* m_rate = nullptr;
    QSpinBox* m_tail = nullptr;
    QCheckBox* m_audio = nullptr;
    QCheckBox* m_metronome = nullptr;
    QLineEdit* m_path = nullptr;
    QLabel* m_preview = nullptr;
    QLabel* m_availabilityLabel = nullptr;
    QWidget* m_controls = nullptr;
    // 版本号用于丢弃过期回调，原子标志用于请求后台渲染取消。
    quint64 m_previewRevision = 0;
    qint64 m_previewPosition = 0;
    bool m_available = true;
    QString m_unavailableReason;
    std::shared_ptr<std::atomic_bool> m_previewCancel;
};

} // namespace midi_play::presentation
