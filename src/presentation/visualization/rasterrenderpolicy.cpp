#include "rasterrenderpolicy.h"

#include <QPainter>

namespace midi_play::presentation::visualization {

void RasterRenderPolicy::apply(QPainter& painter)
{
    painter.setRenderHint(QPainter::Antialiasing, false);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
    painter.setRenderHint(QPainter::TextAntialiasing, true);
}

} // namespace midi_play::presentation::visualization
