#pragma once

#include "domain/playback/playbackcontroller.h"
#include "domain/settings/playersettings.h"
#include "domain/visualization/visualchart.h"
#include "infrastructure/readers/musicreaderregistry.h"

#include <QObject>
#include <QFutureWatcher>
#include <memory>

namespace midi_play::app {

class PlayerApplicationService final : public QObject {
    Q_OBJECT
public:
    explicit PlayerApplicationService(QObject* parent = nullptr);

    playback::PlaybackSession* session() const { return m_controller ? m_controller->session() : nullptr; }
    QString fileName() const { return m_fileName; }
    playback::State playbackState() const { return m_playbackState; }
    qint64 positionMicroseconds() const { return m_positionUs; }
    qint64 durationMicroseconds() const { return m_durationUs; }
    int visualizationRefreshRate() const noexcept { return m_visualizationRefreshRate; }
    int playbackRatePercent() const noexcept { return m_playbackRatePercent; }
    bool metronomeEnabled() const noexcept { return m_metronomeEnabled; }
    bool supportsMetronome() const noexcept { return m_controller && m_controller->supportsMetronome(); }
    QString metronomeUnavailableReason() const
    {
        return m_controller ? m_controller->metronomeUnavailableReason() : QStringLiteral("请先加载乐曲");
    }
    bool hasSoundFont() const noexcept { return !m_soundFontPath.isEmpty(); }
    bool isSoundFontLoading() const noexcept { return m_soundFontLoading; }
    const QString& lastSoundFontError() const noexcept { return m_lastSoundFontError; }

public slots:
    void openFile(const QString& path);
    void openMusicXml(const QString& path);
    bool loadSoundFont(const QString& path);
    void requestSoundFontLoad(const QString& path);
    void setVisualizationRefreshRate(int refreshRate);
    void setPlaybackRatePercent(int percent);
    void setMetronomeEnabled(bool enabled);
    void setGraphicsMode(settings::GraphicsMode mode);
    settings::GraphicsMode graphicsMode() const noexcept { return m_graphicsMode; }
    void play();
    void pause();
    void stop();
    void seek(qint64 microseconds);

signals:
    void busyChanged(bool busy);
    void documentLoaded(const QString& title, qint64 duration);
    void soundFontLoaded(const QString& path);
    void soundFontSelectionCommitted(const QString& path);
    void soundFontLoadFailed(const QString& message);
    void soundFontLoadingChanged(bool loading);
    // Completion of requestSoundFontLoad; playback diagnostics do not emit it.
    void soundFontLoadFinished(bool success);
    void positionChanged(qint64 position, qint64 duration, qint64 sampledAtUs = 0);
    void playbackDiscontinuity(qint64 position);
    void playbackStateChanged(midi_play::playback::State state);
    void visualizationReady(midi_play::visualization::VisualChartPtr chart);
    void graphicsModeChanged(midi_play::settings::GraphicsMode mode);
    void playbackRateChanged(int percent);
    void metronomeChanged(bool enabled);
    void metronomeAvailabilityChanged(bool available, const QString& reason);
    void errorOccurred(const QString& message);

private:
    void connectSession();
    void completeSoundFontLoad(bool success, const QString& error);
    void setSoundFontLoading(bool loading);
    bool normalizeSoundFontPath(const QString& path, QString* normalizedPath);
    bool validateSoundFontFile(const QString& path, QString* normalizedPath);
    void reportSoundFontFailure(const QString& message);

    std::unique_ptr<playback::PlaybackController> m_controller;
    QString m_fileName;
    QString m_soundFontPath;
    QString m_pendingSoundFontPath;
    QString m_lastSoundFontError;
    QFutureWatcher<QString> m_soundFontValidationWatcher;
    bool m_soundFontLoading = false;
    bool m_documentLoading = false;
    readers::MusicReaderRegistry m_readerRegistry;
    playback::State m_playbackState = playback::State::Empty;
    qint64 m_positionUs = 0;
    qint64 m_durationUs = 0;
    quint64 m_loadGeneration = 0;
    int m_visualizationRefreshRate = settings::kDefaultVisualizationRefreshRate;
    int m_playbackRatePercent = settings::kDefaultPlaybackRatePercent;
    bool m_metronomeEnabled = false;
    settings::GraphicsMode m_graphicsMode = settings::kDefaultGraphicsMode;
};

} // namespace midi_play::app
