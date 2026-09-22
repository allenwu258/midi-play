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

    const bool hasGraphicsMode = file.contains(QStringLiteral("General/graphicsMode"));
    const QVariant graphicsModeValue = file.value(QStringLiteral("General/graphicsMode"),
                                                   midi_play::settings::graphicsModePersistentValue(
                                                       midi_play::settings::kDefaultGraphicsMode));
    result.graphicsMode = midi_play::settings::graphicsModeFromPersistentValue(
        graphicsModeValue.toInt());
    result.graphicsModeConfigured = hasGraphicsMode;
    result.showNotationStrip = file.value(QStringLiteral("General/showNotationStrip"),
        midi_play::settings::kDefaultShowNotationStrip).toBool();
    bool themeConversionOk = false;
    const int themeValue = file.value(QStringLiteral("General/themeMode"),
        midi_play::settings::themeModePersistentValue(midi_play::settings::kDefaultThemeMode))
        .toInt(&themeConversionOk);
    result.themeMode = midi_play::settings::themeModeFromPersistentValue(themeValue);
    const bool hasNoteColorMode = file.contains(QStringLiteral("General/noteColorMode"));
    bool noteColorConversionOk = false;
    const int noteColorValue = file.value(QStringLiteral("General/noteColorMode"),
        midi_play::settings::noteColorModePersistentValue(midi_play::settings::kDefaultNoteColorMode))
        .toInt(&noteColorConversionOk);
    result.noteColorMode = noteColorConversionOk
        ? midi_play::settings::noteColorModeFromPersistentValue(noteColorValue)
        : midi_play::settings::kDefaultNoteColorMode;

    const bool hasTitleBarMode = file.contains(QStringLiteral("General/titleBarMode"));
    const QVariant titleBarModeValue = file.value(QStringLiteral("General/titleBarMode"),
                                                  midi_play::settings::titleBarModePersistentValue(
                                                      midi_play::settings::kDefaultTitleBarMode));
    bool titleBarModeConversionOk = false;
    const int configuredTitleBarMode = titleBarModeValue.toInt(&titleBarModeConversionOk);
    result.titleBarMode = midi_play::settings::titleBarModeFromPersistentValue(configuredTitleBarMode);
    result.soundFontPath =
        file.value(QStringLiteral("Audio/soundFontPath")).toString().trimmed();

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
    if (hasTitleBarMode && !titleBarModeConversionOk && warning) {
        *warning = warning->isEmpty()
            ? QStringLiteral("设置文件中的标题栏模式不是整数，已回退到原生标题栏")
            : *warning + QStringLiteral("；标题栏模式不是整数，已回退到原生标题栏");
    } else if (hasTitleBarMode
               && configuredTitleBarMode != midi_play::settings::titleBarModePersistentValue(
                   result.titleBarMode)
               && warning) {
        *warning = warning->isEmpty()
            ? QStringLiteral("设置文件中的标题栏模式当前平台不可用，已回退到原生标题栏")
            : *warning + QStringLiteral("；标题栏模式当前平台不可用，已回退到原生标题栏");
    }
    if ((!themeConversionOk || !midi_play::settings::isValidThemeMode(themeValue)) && warning) {
        const auto message = QStringLiteral("设置文件中的主题无效，已回退到深色主题");
        *warning = warning->isEmpty() ? message : *warning + QStringLiteral("；") + message;
    }
    if (hasNoteColorMode
        && (!noteColorConversionOk || !midi_play::settings::isValidNoteColorMode(noteColorValue))
        && warning) {
        const auto message = QStringLiteral("设置文件中的音符色彩模式无效，已回退到鲜明模式");
        *warning = warning->isEmpty() ? message : *warning + QStringLiteral("；") + message;
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
    file.setValue(QStringLiteral("General/graphicsMode"),
                  midi_play::settings::graphicsModePersistentValue(settings.graphicsMode));
    file.setValue(QStringLiteral("General/showNotationStrip"), settings.showNotationStrip);
    file.setValue(QStringLiteral("General/themeMode"),
                  midi_play::settings::themeModePersistentValue(settings.themeMode));
    file.setValue(QStringLiteral("General/noteColorMode"),
                  midi_play::settings::noteColorModePersistentValue(settings.noteColorMode));
    file.setValue(QStringLiteral("General/titleBarMode"),
                  midi_play::settings::titleBarModePersistentValue(settings.titleBarMode));
    if (settings.soundFontPath.isEmpty()) {
        file.remove(QStringLiteral("Audio/soundFontPath"));
    } else {
        file.setValue(QStringLiteral("Audio/soundFontPath"), settings.soundFontPath);
    }
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
