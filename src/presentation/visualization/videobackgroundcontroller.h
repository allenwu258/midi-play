#pragma once
#include "infrastructure/media/videoframereader.h"
#include <QObject>
#include <QFutureWatcher>
#include <memory>

class QAudioOutput;
class QMediaPlayer;
namespace midi_play::presentation::visualization {
// GUI-thread coordinator. One in-flight decode and one replaceable request keep
// rapid scrubbing bounded. Decoders and cancellation tokens outlive their jobs.
class VideoBackgroundController final : public QObject {
    Q_OBJECT
public:
    explicit VideoBackgroundController(QObject* parent = nullptr);
    ~VideoBackgroundController() override;
    void setSource(const QString& path, int maximumDimension);
    void setMuted(bool muted);
    void synchronize(qint64 musicUs, qint64 durationUs, bool playing, int ratePercent,
                     bool discontinuity = false);
signals:
    void frameReady(const QImage& image);
    void errorOccurred(const QString& error);
private:
    struct Worker;
    struct Result {
        media::VideoFrame frame;
        media::VideoMetadata metadata;
        QString error;
    };
    void requestFrame();
    void synchronizeAudio(bool discontinuity);
    void clearAudioSource();
    std::shared_ptr<Worker> m_worker;
    QFutureWatcher<Result>* m_watcher = nullptr;
    QAudioOutput* m_audioOutput = nullptr;
    QMediaPlayer* m_audioPlayer = nullptr;
    media::VideoMetadata m_metadata;
    QString m_path;
    int m_dimension = 2048, m_rate = 100;
    qint64 m_musicUs = 0, m_durationUs = 0, m_audioLoop = -1;
    quint64 m_epoch = 0;
    bool m_playing = false, m_muted = true, m_pending = false, m_failed = false;
    bool m_audioSynchronizing = false, m_audioFailed = false;
};
}
