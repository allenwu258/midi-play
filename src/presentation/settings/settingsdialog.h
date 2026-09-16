#pragma once

#include <QDialog>
#include "domain/settings/titlebarmode.h"
#include "domain/settings/graphicsmode.h"
#include "domain/settings/thememode.h"

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;

namespace midi_play::app { class SettingsService; }
namespace midi_play::app { class PlayerApplicationService; }
namespace midi_play::presentation::theme { class ThemeController; }

namespace midi_play::presentation::settings {

class SettingsDialog final : public QDialog {
    Q_OBJECT
public:
    explicit SettingsDialog(app::SettingsService* settingsService,
                            app::PlayerApplicationService* playerService,
                            QWidget* parent = nullptr,
                            theme::ThemeController* themeController = nullptr);

private slots:
    void applyRefreshRateFromUi();
    void applyCustomRefreshRateFromUi();
    void applyTitleBarModeFromUi();
    void applyGraphicsModeFromUi();
    void chooseSoundFont();
    void resetSoundFont();
    void updateRefreshRateSelection(int refreshRate);
    void updateTitleBarModeSelection(midi_play::settings::TitleBarMode mode);
    void updateGraphicsModeSelection(midi_play::settings::GraphicsMode mode);
    void updateNotationStripSelection(bool show);
    void updateThemeSelection(midi_play::settings::ThemeMode mode);
    void applyTheme(midi_play::settings::ThemeMode mode);
    void updateSoundFontPath(const QString& path, bool usesDefault);
    void setSoundFontLoading(bool loading);
    void showSaveError(const QString& message);

private:
    void initializeRefreshRateOptions();

    app::SettingsService* m_settingsService = nullptr;
    app::PlayerApplicationService* m_playerService = nullptr;
    QComboBox* m_refreshRateCombo = nullptr;
    QComboBox* m_titleBarModeCombo = nullptr;
    QComboBox* m_graphicsModeCombo = nullptr;
    QComboBox* m_themeCombo = nullptr;
    QCheckBox* m_showNotationStripCheckBox = nullptr;
    QLabel* m_customRefreshRateLabel = nullptr;
    QSpinBox* m_customRefreshRateSpinBox = nullptr;
    QLineEdit* m_soundFontPathEdit = nullptr;
    QPushButton* m_loadSoundFontButton = nullptr;
    QPushButton* m_resetSoundFontButton = nullptr;
    QLabel* m_errorLabel = nullptr;
    bool m_soundFontLoading = false;
};

} // namespace midi_play::presentation::settings
