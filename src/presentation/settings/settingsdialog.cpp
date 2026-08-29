#include "settingsdialog.h"

#include "app/settingsservice.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QSignalBlocker>
#include <QVBoxLayout>

namespace midi_play::presentation::settings {

SettingsDialog::SettingsDialog(app::SettingsService* settingsService, QWidget* parent)
    : QDialog(parent), m_settingsService(settingsService)
{
    setWindowTitle(QStringLiteral("设置"));
    setWindowFlag(Qt::Window, true);
    setModal(false);
    resize(360, 180);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(18, 16, 18, 14);
    root->setSpacing(12);

    auto* title = new QLabel(QStringLiteral("播放设置"), this);
    title->setObjectName(QStringLiteral("settingsTitle"));
    root->addWidget(title);

    auto* form = new QFormLayout();
    form->setContentsMargins(0, 0, 0, 0);
    form->setHorizontalSpacing(14);
    form->setVerticalSpacing(10);

    m_refreshRateCombo = new QComboBox(this);
    m_refreshRateCombo->setObjectName(QStringLiteral("refreshRateCombo"));
    initializeRefreshRateOptions();
    form->addRow(QStringLiteral("视觉刷新率"), m_refreshRateCombo);
    root->addLayout(form);

    auto* hint = new QLabel(QStringLiteral("仅影响下落音符和界面刷新，不影响音频播放精度。"), this);
    hint->setObjectName(QStringLiteral("settingsHint"));
    hint->setWordWrap(true);
    root->addWidget(hint);

    m_errorLabel = new QLabel(this);
    m_errorLabel->setObjectName(QStringLiteral("settingsError"));
    m_errorLabel->setWordWrap(true);
    m_errorLabel->hide();
    root->addWidget(m_errorLabel);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::close);
    root->addWidget(buttons);

    setStyleSheet(QStringLiteral(R"(
        QDialog { background: #1b1d20; color: #f0f1ed; }
        QLabel#settingsTitle { font-size: 16px; font-weight: 600; }
        QLabel#settingsHint { color: #aeb4af; font-size: 12px; }
        QLabel#settingsError { color: #ffb4a8; font-size: 12px; }
        QComboBox { min-height: 28px; padding: 2px 8px; background: #25282b; color: #f0f1ed; border: 1px solid #3a3e41; }
        QComboBox:hover { border-color: #555b5f; }
        QPushButton { min-width: 72px; min-height: 28px; color: #dfe1dc; background: #25282b; border: 1px solid #3a3e41; }
        QPushButton:hover { background: #2d3033; }
    )"));

    if (m_settingsService) {
        updateRefreshRateSelection(m_settingsService->visualizationRefreshRate());
        connect(m_refreshRateCombo, qOverload<int>(&QComboBox::currentIndexChanged),
                this, &SettingsDialog::applyRefreshRateFromUi);
        connect(m_settingsService, &app::SettingsService::visualizationRefreshRateChanged,
                this, &SettingsDialog::updateRefreshRateSelection);
        connect(m_settingsService, &app::SettingsService::settingsSaveFailed,
                this, &SettingsDialog::showSaveError);
    } else {
        m_refreshRateCombo->setEnabled(false);
        showSaveError(QStringLiteral("设置服务不可用"));
    }
}

void SettingsDialog::applyRefreshRateFromUi()
{
    if (!m_settingsService || !m_refreshRateCombo) {
        return;
    }

    const int refreshRate = m_refreshRateCombo->currentData().toInt();
    m_errorLabel->hide();
    m_settingsService->setVisualizationRefreshRate(refreshRate);
}

void SettingsDialog::updateRefreshRateSelection(int refreshRate)
{
    if (!m_refreshRateCombo) {
        return;
    }

    const int index = m_refreshRateCombo->findData(refreshRate);
    if (index < 0 || index == m_refreshRateCombo->currentIndex()) {
        return;
    }

    const QSignalBlocker blocker(m_refreshRateCombo);
    m_refreshRateCombo->setCurrentIndex(index);
}

void SettingsDialog::showSaveError(const QString& message)
{
    if (!m_errorLabel) {
        return;
    }

    m_errorLabel->setText(message);
    m_errorLabel->show();
}

void SettingsDialog::initializeRefreshRateOptions()
{
    m_refreshRateCombo->addItem(QStringLiteral("30 FPS"), 30);
    m_refreshRateCombo->addItem(QStringLiteral("60 FPS"), 60);
    m_refreshRateCombo->addItem(QStringLiteral("120 FPS"), 120);
}

} // namespace midi_play::presentation::settings
