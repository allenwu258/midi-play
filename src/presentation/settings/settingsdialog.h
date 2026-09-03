#pragma once

#include <QDialog>
#include "domain/settings/titlebarmode.h"

class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;

namespace midi_play::app { class SettingsService; }
namespace midi_play::app { class PlayerApplicationService; }

namespace midi_play::presentation::settings {

class SettingsDialog final : public QDialog {
    Q_OBJECT
public:
    explicit SettingsDialog(app::SettingsService* settingsService,
                            app::PlayerApplicationService* playerService,
                            QWidget* parent = nullptr);

private slots:
    void applyRefreshRateFromUi();
    void applyCustomRefreshRateFromUi();
    void applyTitleBarModeFromUi();
    void chooseSoundFont();
    void resetSoundFont();
    void updateRefreshRateSelection(int refreshRate);
    void updateTitleBarModeSelection(midi_play::settings::TitleBarMode mode);
    void updateSoundFontPath(const QString& path, bool usesDefault);
    void showSaveError(const QString& message);

private:
    void initializeRefreshRateOptions();

    app::SettingsService* m_settingsService = nullptr;
    app::PlayerApplicationService* m_playerService = nullptr;
    QComboBox* m_refreshRateCombo = nullptr;
    QComboBox* m_titleBarModeCombo = nullptr;
    QLabel* m_customRefreshRateLabel = nullptr;
    QSpinBox* m_customRefreshRateSpinBox = nullptr;
    QLineEdit* m_soundFontPathEdit = nullptr;
    QPushButton* m_loadSoundFontButton = nullptr;
    QPushButton* m_resetSoundFontButton = nullptr;
    QLabel* m_errorLabel = nullptr;
};

} // namespace midi_play::presentation::settings
