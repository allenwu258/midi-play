#pragma once
#include "app/videoexportservice.h"
#include <QDialog>

class QComboBox;
class QCheckBox;
class QSpinBox;
class QLineEdit;
class QLabel;
namespace midi_play::presentation {
class VideoExportDialog final : public QDialog {
public:
    explicit VideoExportDialog(const app::VideoExportOptions& defaults, qint64 previewPositionUs, QWidget* parent = nullptr);
    ~VideoExportDialog() override;
    app::VideoExportOptions options() const;
private:
    void updatePreview();
    app::VideoExportOptions m_defaults;
    QComboBox* m_size;
    QComboBox* m_fps;
    QComboBox* m_quality;
    QSpinBox* m_rate;
    QSpinBox* m_tail;
    QCheckBox* m_audio;
    QCheckBox* m_metronome;
    QLineEdit* m_path;
    QLabel* m_preview;
    quint64 m_previewRevision = 0;
    qint64 m_previewPosition;
    std::shared_ptr<std::atomic_bool> m_previewCancel;
};
} // namespace midi_play::presentation
