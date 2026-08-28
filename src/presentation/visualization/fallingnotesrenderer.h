#pragma once

#include "domain/visualization/playbackscenestate.h"
#include "noterendercache.h"
#include "scenegeometry.h"

#include <QColor>
#include <QLineF>
#include <QRectF>
#include <QVector>

class QPainter;

namespace midi_play::presentation::visualization {

struct VisualizationTheme {
    QColor background = QColor("#121416");
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
                const midi_play::visualization::PlaybackSceneState& state);
    void renderStaticLayer(QPainter& painter, const PlaybackSceneGeometry& geometry,
                           const midi_play::visualization::PlaybackSceneState& state);
    void renderStaticBackgroundLayer(QPainter& painter, const PlaybackSceneGeometry& geometry,
                                     const midi_play::visualization::PlaybackSceneState& state);
    void renderStaticKeyboardLayer(QPainter& painter, const PlaybackSceneGeometry& geometry,
                                   const midi_play::visualization::PlaybackSceneState& state);
    void renderDynamicLayer(QPainter& painter, const PlaybackSceneGeometry& geometry,
                            const midi_play::visualization::PlaybackSceneState& state);

private:
    struct NoteStyleBatch {
        QVector<QRectF> tails;
        QVector<QRectF> inactiveBodies;
        QVector<QRectF> activeBodies;
        QVector<QLineF> inactiveAttackLines;
        QVector<QLineF> activeAttackLines;

        void clear()
        {
            tails.clear();
            inactiveBodies.clear();
            activeBodies.clear();
            inactiveAttackLines.clear();
            activeAttackLines.clear();
        }
    };

    void prepareScene(const PlaybackSceneGeometry& geometry,
                      const midi_play::visualization::PlaybackSceneState& state);
    void drawPitchBands(QPainter& painter, const PlaybackSceneGeometry& geometry) const;
    void drawTimeGrid(QPainter& painter, const PlaybackSceneGeometry& geometry,
                      const midi_play::visualization::PlaybackSceneState& state) const;
    void drawNotes(QPainter& painter, const PlaybackSceneGeometry& geometry,
                   const midi_play::visualization::PlaybackSceneState& state);
    void drawStrikeLine(QPainter& painter, const PlaybackSceneGeometry& geometry,
                        const midi_play::visualization::PlaybackSceneState& state) const;
    void drawKeyboardBase(QPainter& painter, const PlaybackSceneGeometry& geometry,
                          const midi_play::visualization::PlaybackSceneState& state) const;
    void drawActiveKeyboard(QPainter& painter, const PlaybackSceneGeometry& geometry,
                            const midi_play::visualization::PlaybackSceneState& state) const;
    void drawOverlay(QPainter& painter, const PlaybackSceneGeometry& geometry,
                     const midi_play::visualization::PlaybackSceneState& state) const;

    VisualizationTheme m_theme;
    NoteRenderCache m_noteRenderCache;
    QVector<NoteStyleBatch> m_noteBatches;
    QVector<quint32> m_batchEpochs;
    QVector<int> m_usedStyleIndices;
    QVector<QLineF> m_tremoloLines;
    QVector<QRectF> m_pitchBandRects;
    QVector<QRectF> m_blackKeyForegroundRects;
    quint64 m_keyboardGeometryBuildCount = 0;
    quint32 m_batchEpoch = 0;
};

} // namespace midi_play::presentation::visualization
