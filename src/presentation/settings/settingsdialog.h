#pragma once

#include <QDialog>
#include "domain/settings/titlebarmode.h"

class QComboBox;
class QLabel;
class QSpinBox;

namespace midi_play::app { class SettingsService; }

namespace midi_play::presentation::settings {

class SettingsDialog final : public QDialog {
    Q_OBJECT
public:
    explicit SettingsDialog(app::SettingsService* settingsService, QWidget* parent = nullptr);

private slots:
    void applyRefreshRateFromUi();
    void applyCustomRefreshRateFromUi();
    void applyTitleBarModeFromUi();
    void updateRefreshRateSelection(int refreshRate);
    void updateTitleBarModeSelection(midi_play::settings::TitleBarMode mode);
    void showSaveError(const QString& message);

private:
    void initializeRefreshRateOptions();

    app::SettingsService* m_settingsService = nullptr;
    QComboBox* m_refreshRateCombo = nullptr;
    QComboBox* m_titleBarModeCombo = nullptr;
    QLabel* m_customRefreshRateLabel = nullptr;
    QSpinBox* m_customRefreshRateSpinBox = nullptr;
    QLabel* m_errorLabel = nullptr;
};

} // namespace midi_play::presentation::settings
