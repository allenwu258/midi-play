#include "qsettingsstore.h"

#include <QDir>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>
#include <QVariant>

#include <utility>
#include <algorithm>

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
    result.ffmpegPath =
        file.value(QStringLiteral("Video/ffmpegPath")).toString().trimmed();
    result.ffmpegUsePath = file.value(QStringLiteral("Video/ffmpegUsePath"), true).toBool();
    result.backgroundImagePath =
        file.value(QStringLiteral("Visualization/backgroundImagePath")).toString().trimmed();
    result.backgroundImageEnabled = file.value(
        QStringLiteral("Visualization/backgroundImageEnabled"), !result.backgroundImagePath.isEmpty()).toBool();
    const bool hasBackgroundAlignment = file.contains(QStringLiteral("Visualization/backgroundImageAlignment"));
    const QVariant backgroundAlignmentValue = file.value(
        QStringLiteral("Visualization/backgroundImageAlignment"),
        midi_play::settings::backgroundImageAlignmentPersistentValue(
            midi_play::settings::kDefaultBackgroundImageAlignment));
    bool backgroundAlignmentConversionOk = false;
    const int configuredBackgroundAlignment = backgroundAlignmentValue.toInt(&backgroundAlignmentConversionOk);
    result.backgroundImageAlignment = backgroundAlignmentConversionOk
        ? midi_play::settings::backgroundImageAlignmentFromPersistentValue(configuredBackgroundAlignment)
        : midi_play::settings::kDefaultBackgroundImageAlignment;
    const bool hasBackgroundOpacity = file.contains(QStringLiteral("Visualization/backgroundImageOpacity"));
    const QVariant backgroundOpacityValue = file.value(
        QStringLiteral("Visualization/backgroundImageOpacity"),
        midi_play::settings::kDefaultBackgroundImageOpacity);
    bool backgroundOpacityConversionOk = false;
    const int configuredBackgroundOpacity = backgroundOpacityValue.toInt(&backgroundOpacityConversionOk);
    result.backgroundImageOpacity = backgroundOpacityConversionOk
        ? std::clamp(configuredBackgroundOpacity, 0, 100)
        : midi_play::settings::kDefaultBackgroundImageOpacity;
    result.visualEffectsEnabled = file.value(
        QStringLiteral("Visualization/visualEffectsEnabled"), true).toBool();
    const bool hasVisualEffectsLevel = file.contains(QStringLiteral("Visualization/visualEffectsLevel"));
    const QVariant visualEffectsLevelValue = file.value(
        QStringLiteral("Visualization/visualEffectsLevel"),
        midi_play::settings::visualEffectLevelPersistentValue(
            midi_play::settings::kDefaultVisualEffectLevel));
    bool visualEffectsLevelConversionOk = false;
    const int configuredVisualEffectsLevel = visualEffectsLevelValue.toInt(&visualEffectsLevelConversionOk);
    result.visualEffectsLevel = visualEffectsLevelConversionOk
        ? midi_play::settings::visualEffectLevelFromPersistentValue(configuredVisualEffectsLevel)
        : midi_play::settings::kDefaultVisualEffectLevel;

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
    if (hasVisualEffectsLevel
        && (!visualEffectsLevelConversionOk
            || !midi_play::settings::isValidVisualEffectLevel(configuredVisualEffectsLevel))
        && warning) {
        const auto message = QStringLiteral("设置文件中的流光特效强度无效，已回退到中档");
        *warning = warning->isEmpty() ? message : *warning + QStringLiteral("；") + message;
    }
    if (hasBackgroundAlignment
        && (!backgroundAlignmentConversionOk
            || !midi_play::settings::isValidBackgroundImageAlignment(configuredBackgroundAlignment))
        && warning) {
        const auto message = QStringLiteral("设置文件中的背景适配方式无效，已回退到裁剪铺满");
        *warning = warning->isEmpty() ? message : *warning + QStringLiteral("；") + message;
    }
    if (hasBackgroundOpacity
        && (!backgroundOpacityConversionOk || configuredBackgroundOpacity < 0 || configuredBackgroundOpacity > 100)
        && warning) {
        const auto message = QStringLiteral("设置文件中的背景不透明度无效，已限制到 0%–100% 范围");
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
    if (settings.ffmpegPath.isEmpty()) {
        file.remove(QStringLiteral("Video/ffmpegPath"));
    } else {
        file.setValue(QStringLiteral("Video/ffmpegPath"), settings.ffmpegPath);
    }
    file.setValue(QStringLiteral("Video/ffmpegUsePath"), settings.ffmpegUsePath);
    if (settings.backgroundImagePath.isEmpty()) {
        file.remove(QStringLiteral("Visualization/backgroundImagePath"));
    } else {
        file.setValue(QStringLiteral("Visualization/backgroundImagePath"), settings.backgroundImagePath);
    }
    file.setValue(QStringLiteral("Visualization/backgroundImageEnabled"), settings.backgroundImageEnabled);
    file.setValue(QStringLiteral("Visualization/backgroundImageAlignment"),
                  midi_play::settings::backgroundImageAlignmentPersistentValue(settings.backgroundImageAlignment));
    file.setValue(QStringLiteral("Visualization/backgroundImageOpacity"),
                  std::clamp(settings.backgroundImageOpacity, 0, 100));
    file.setValue(QStringLiteral("Visualization/visualEffectsEnabled"), settings.visualEffectsEnabled);
    file.setValue(QStringLiteral("Visualization/visualEffectsLevel"),
                  midi_play::settings::visualEffectLevelPersistentValue(settings.visualEffectsLevel));
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
