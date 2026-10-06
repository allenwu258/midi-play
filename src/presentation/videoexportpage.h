#pragma once

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

class VideoExportPage final : public QWidget {
public:
    explicit VideoExportPage(const app::VideoExportOptions& defaults,
                             qint64 previewPositionUs, QWidget* parent = nullptr);
    ~VideoExportPage() override;

    app::VideoExportOptions options() const;
    QString outputPath() const;
    void setAvailability(bool available, const QString& reason);
    bool isAvailable() const noexcept { return m_available; }
    QString validationError() const;

private:
    void updatePreview();

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
    quint64 m_previewRevision = 0;
    qint64 m_previewPosition = 0;
    bool m_available = true;
    QString m_unavailableReason;
    std::shared_ptr<std::atomic_bool> m_previewCancel;
};

} // namespace midi_play::presentation
