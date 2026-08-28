#pragma once

class QPainter;

namespace midi_play::presentation::visualization {

// Defines the render-hint contract for the QWidget raster backend. Geometry
// is intentionally aliased for predictable, low-cost axis-aligned drawing;
// text keeps its independent antialiasing path for readability.
class RasterRenderPolicy final {
public:
    static void apply(QPainter& painter);
};

} // namespace midi_play::presentation::visualization
