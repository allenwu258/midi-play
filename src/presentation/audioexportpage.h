#pragma once

#include "app/audioexportservice.h"

#include <QWidget>

class QCheckBox;
class QComboBox;
class QLineEdit;
class QSpinBox;

namespace midi_play::presentation {

class AudioExportPage final : public QWidget {
public:
    AudioExportPage(const QString& sourcePath, const QString& soundFontPath,
                    QWidget* parent = nullptr);

    app::AudioExportOptions options() const;
    bool hasSoundFont() const noexcept { return !m_soundFontPath.isEmpty(); }
    QString outputPath() const;
    QString validationError() const;

private:
    QString m_soundFontPath;
    QComboBox* m_format = nullptr;
    QComboBox* m_bitrate = nullptr;
    QComboBox* m_sampleRate = nullptr;
    QCheckBox* m_metronome = nullptr;
    QSpinBox* m_tail = nullptr;
    QLineEdit* m_path = nullptr;
};

} // namespace midi_play::presentation
