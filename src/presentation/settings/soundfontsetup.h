#pragma once

#include <QDialog>
#include <QObject>

namespace midi_play::app { class PlayerApplicationService; class SettingsService; }
namespace midi_play::presentation::theme { class ThemeController; }

namespace midi_play::presentation::settings {

// Shared by initial setup and the settings page; selection is committed only
// after the application service has successfully loaded the candidate.
void chooseSoundFontFile(QWidget* parent, app::SettingsService* settings,
                         app::PlayerApplicationService* player);

class SoundFontSetupDialog final : public QDialog {
    Q_OBJECT
public:
    SoundFontSetupDialog(app::SettingsService* settings, app::PlayerApplicationService* player,
                         QWidget* parent, theme::ThemeController* themeController = nullptr);
    void reject() override;

private:
    app::PlayerApplicationService* m_player;
};

// One startup check per process. Runtime playback/selection failures must not
// reopen the onboarding dialog after the user has chosen to skip it.
class SoundFontSetup final : public QObject {
    Q_OBJECT
public:
    SoundFontSetup(app::SettingsService* settings, app::PlayerApplicationService* player,
                   QWidget* parent, theme::ThemeController* themeController = nullptr);
    void start();

signals:
    void finished();

private:
    void showDialog();
    app::SettingsService* m_settings;
    app::PlayerApplicationService* m_player;
    QWidget* m_window;
    theme::ThemeController* m_themeController;
    bool m_started = false;
    bool m_checkPending = false;
};

} // namespace midi_play::presentation::settings
