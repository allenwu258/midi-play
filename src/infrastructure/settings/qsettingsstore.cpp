#include "qsettingsstore.h"

#include <QDir>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>
#include <QVariant>

#include <utility>

namespace midi_play::infrastructure::settings {
namespace {

QString statusMessage(QSettings::Status status)
{
    switch (status) {
    case QSettings::NoError:
        return {};
    case QSettings::AccessError:
        return QStringLiteral("无法访问设置文件");
    case QSettings::FormatError:
        return QStringLiteral("设置文件格式错误");
    }
    return QStringLiteral("设置文件状态异常");
}

} // namespace

QSettingsStore::QSettingsStore(QString settingsPath)
    : m_settingsPath(settingsPath.isEmpty() ? defaultSettingsPath() : std::move(settingsPath))
{
}

midi_play::settings::PlayerSettings QSettingsStore::load(QString* warning)
{
    midi_play::settings::PlayerSettings result;
    if (warning) warning->clear();
    if (m_settingsPath.isEmpty()) {
        if (warning) *warning = QStringLiteral("无法定位用户级设置目录，已使用默认设置");
        return result;
    }

    QSettings file(m_settingsPath, QSettings::IniFormat);
    result.schemaVersion = file.value(QStringLiteral("General/schemaVersion"),
                                      midi_play::settings::kSettingsSchemaVersion).toInt();
    const bool hasRefreshRate = file.contains(QStringLiteral("General/visualizationRefreshRate"));
    const QVariant refreshRateValue = file.value(QStringLiteral("General/visualizationRefreshRate"),
                                                 midi_play::settings::kDefaultVisualizationRefreshRate);
    bool refreshRateConversionOk = false;
    const int configuredRefreshRate = refreshRateValue.toInt(&refreshRateConversionOk);
    result.visualizationRefreshRate =
        midi_play::settings::normalizeVisualizationRefreshRate(configuredRefreshRate);

    const QString readStatus = statusMessage(file.status());
    if (!readStatus.isEmpty() && warning) {
        *warning = QStringLiteral("%1: %2，已使用默认设置").arg(readStatus, m_settingsPath);
        return {};
    }
    if (hasRefreshRate && !refreshRateConversionOk && warning) {
        *warning = QStringLiteral("设置文件中的刷新率不是整数，已回退到 60 FPS");
    } else if (hasRefreshRate
               && !midi_play::settings::isValidVisualizationRefreshRate(configuredRefreshRate)
               && warning) {
        *warning = QStringLiteral("设置文件中的刷新率无效: %1，已回退到 60 FPS").arg(configuredRefreshRate);
    }
    return result;
}

bool QSettingsStore::save(const midi_play::settings::PlayerSettings& settings, QString* error)
{
    if (error) error->clear();
    if (m_settingsPath.isEmpty()) {
        if (error) *error = QStringLiteral("无法定位用户级设置目录");
        return false;
    }

    const QFileInfo fileInfo(m_settingsPath);
    QDir directory(fileInfo.absolutePath());
    if (!directory.exists() && !directory.mkpath(QStringLiteral("."))) {
        if (error) *error = QStringLiteral("无法创建设置目录: %1").arg(directory.absolutePath());
        return false;
    }

    QSettings file(m_settingsPath, QSettings::IniFormat);
    file.setValue(QStringLiteral("General/schemaVersion"),
                  midi_play::settings::kSettingsSchemaVersion);
    file.setValue(QStringLiteral("General/visualizationRefreshRate"),
                  midi_play::settings::normalizeVisualizationRefreshRate(settings.visualizationRefreshRate));
    file.sync();

    const QString writeStatus = statusMessage(file.status());
    if (!writeStatus.isEmpty()) {
        if (error) *error = QStringLiteral("%1: %2").arg(writeStatus, m_settingsPath);
        return false;
    }
    return true;
}

QString QSettingsStore::defaultSettingsPath()
{
    const QString directory =
        QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    if (directory.isEmpty()) {
        return {};
    }
    return QDir(directory).filePath(QStringLiteral("settings.ini"));
}

} // namespace midi_play::infrastructure::settings
