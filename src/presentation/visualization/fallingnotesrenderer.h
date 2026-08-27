#pragma once

#include "domain/visualization/playbackscenestate.h"
#include "scenegeometry.h"

#include <QColor>

class QPainter;

namespace midi_play::presentation::visualization {

struct VisualizationTheme {
    QColor background = QColor("#121416");
    QColor informationBackground = QColor("#191c1f");
    QColor keyboardBackground = QColor("#101214");
    QColor primaryText = QColor("#f0f1ed");
    QColor secondaryText = QColor("#9da29f");
    QColor subtleText = QColor("#737975");
    QColor beatLine = QColor(255, 255, 255, 18);
    QColor measureLine = QColor(255, 255, 255, 48);
    QColor strikeLine = QColor("#f4d35e");
    QColor whiteKey = QColor("#dedfd9");
    QColor whiteKeyBorder = QColor("#80847f");
    QColor blackKey = QColor("#24272a");
    QColor blackKeyBorder = QColor("#090a0b");
    QColor error = QColor("#ef656b");
};

class FallingNotesRenderer final {
public:
    void render(QPainter& painter, const PlaybackSceneGeometry& geometry,
                const midi_play::visualization::PlaybackSceneState& state) const;

private:
    void drawInformation(QPainter& painter, const PlaybackSceneGeometry& geometry,
                         const midi_play::visualization::PlaybackSceneState& state) const;
    void drawGrid(QPainter& painter, const PlaybackSceneGeometry& geometry,
                  const midi_play::visualization::PlaybackSceneState& state) const;
    void drawNotes(QPainter& painter, const PlaybackSceneGeometry& geometry,
                   const midi_play::visualization::PlaybackSceneState& state) const;
    void drawStrikeLine(QPainter& painter, const PlaybackSceneGeometry& geometry,
                        const midi_play::visualization::PlaybackSceneState& state) const;
    void drawKeyboard(QPainter& painter, const PlaybackSceneGeometry& geometry,
                      const midi_play::visualization::PlaybackSceneState& state) const;
    void drawOverlay(QPainter& painter, const PlaybackSceneGeometry& geometry,
                     const midi_play::visualization::PlaybackSceneState& state) const;

    VisualizationTheme m_theme;
};

} // namespace midi_play::presentation::visualization
