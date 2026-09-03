#include "defaultsoundfontlocator.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>

namespace midi_play::resources {

QString DefaultSoundFontLocator::locate()
{
    const QString relative = relativePath();
    const QString installedPath =
        QDir(QCoreApplication::applicationDirPath()).absoluteFilePath(relative);
    if (QFileInfo::exists(installedPath)) {
        return QDir::cleanPath(installedPath);
    }

    const QString workingTreePath = QDir::current().absoluteFilePath(relative);
    if (QFileInfo::exists(workingTreePath)) {
        return QDir::cleanPath(workingTreePath);
    }

    // Preserve a deterministic expected path so the UI can diagnose a broken
    // package and resetting never captures a machine-specific working folder.
    return QDir::cleanPath(installedPath);
}

QString DefaultSoundFontLocator::relativePath()
{
    return QStringLiteral("assets/midisound.sf2");
}

} // namespace midi_play::resources
