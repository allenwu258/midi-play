#pragma once

#include "app/audioexportservice.h"
#include "app/videoexportservice.h"

#include <QDialog>

class QDialogButtonBox;
class QTabWidget;

namespace midi_play::presentation {
class AudioExportPage;
class VideoExportPage;

class ExportDialog final : public QDialog {
public:
    enum class ExportType { Audio, Video };

    ExportDialog(const QString& sourcePath, const QString& soundFontPath,
                 const app::VideoExportOptions& videoDefaults, qint64 previewPositionUs,
                 bool videoAvailable, const QString& videoUnavailableReason,
                 QWidget* parent = nullptr);

    ExportType exportType() const noexcept;
    app::AudioExportOptions audioOptions() const;
    app::VideoExportOptions videoOptions() const;

private:
    void acceptExport();

    QTabWidget* m_tabs = nullptr;
    AudioExportPage* m_audioPage = nullptr;
    VideoExportPage* m_videoPage = nullptr;
    QDialogButtonBox* m_buttons = nullptr;
};
} // namespace midi_play::presentation
