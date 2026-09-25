#pragma once

#include <QImage>
#include <QString>

namespace midi_play::presentation::visualization {

bool canReadBackgroundImage(const QString& path, QString* error = nullptr);
QImage loadBackgroundImage(const QString& path);

} // namespace midi_play::presentation::visualization
