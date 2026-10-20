#include "videobackgroundcontroller.h"
#include "domain/visualization/backgroundvideotimeline.h"
#include <QAudioOutput>
#include <QMediaPlayer>
#include <QMediaMetaData>
#include <QUrl>
#include <QScopedValueRollback>
#include <QtConcurrent/QtConcurrentRun>
#include <algorithm>

namespace midi_play::presentation::visualization {
struct VideoBackgroundController::Worker {
    media::VideoFrameReader reader;
    std::atomic_bool canceled {false};
    bool opened = false;
};

VideoBackgroundController::VideoBackgroundController(QObject* parent) : QObject(parent) {}
VideoBackgroundController::~VideoBackgroundController()
{
    if (m_worker) m_worker->canceled.store(true);
}

void VideoBackgroundController::setSource(const QString& path, int dimension)
{
    if (m_path == path && m_dimension == dimension) return;
    if (m_worker) m_worker->canceled.store(true);
    ++m_epoch;
    m_path = path; m_dimension = dimension; m_metadata = {};
    m_failed = false; m_pending = false; m_audioLoop = -1;
    m_audioEndedLoop = -1; m_audioFailed = false;
    if (m_audioPlayer) { m_audioPlayer->stop(); m_audioPlayer->setSource({}); }
    m_worker = path.isEmpty() ? nullptr : std::make_shared<Worker>();
    if (m_worker) requestFrame();
}

void VideoBackgroundController::setMuted(bool muted)
{
    if (m_muted == muted) return;
    m_muted = muted;
    m_audioEndedLoop = -1; m_audioFailed = false;
    if (muted && m_audioPlayer) { m_audioPlayer->stop(); m_audioPlayer->setSource({}); }
    synchronizeAudio(true);
}

void VideoBackgroundController::synchronize(qint64 musicUs, qint64 durationUs, bool playing,
                                            int ratePercent, bool discontinuity)
{
    // Small backwards corrections are normal when a transport sample replaces
    // an extrapolated display time. They must not starve the in-flight decoder.
    discontinuity |= musicUs + 100'000 < m_musicUs || musicUs - m_musicUs > 250'000;
    // A seek replaces the pending target, not the running decoder. Let one
    // in-flight frame finish so continuous dragging cannot starve presentation.
    // Source changes still cancel and invalidate all results through m_epoch.
    const bool changed = musicUs != m_musicUs || durationUs != m_durationUs;
    const bool audioChanged = discontinuity || playing != m_playing || ratePercent != m_rate;
    m_musicUs = musicUs; m_durationUs = durationUs; m_playing = playing; m_rate = ratePercent;
    synchronizeAudio(audioChanged);
    if (changed || discontinuity) requestFrame();
}

void VideoBackgroundController::requestFrame()
{
    if (!m_worker || m_failed) return;
    if (m_watcher) { m_pending = true; return; }
    const auto worker = m_worker;
    const auto epoch = m_epoch;
    const auto path = m_path;
    const int dimension = m_dimension;
    const qint64 musicUs = m_musicUs, durationUs = m_durationUs;
    auto* watcher = new QFutureWatcher<Result>(this);
    m_watcher = watcher;
    connect(watcher, &QFutureWatcher<Result>::finished, this, [this, watcher, worker, epoch] {
        const Result result = watcher->result();
        m_watcher = nullptr; watcher->deleteLater();
        if (worker == m_worker && epoch == m_epoch) {
            if (!result.error.isEmpty()) {
                m_failed = true;
                if (m_audioPlayer) m_audioPlayer->stop();
                emit errorOccurred(QStringLiteral("视频背景：%1").arg(result.error));
            } else {
                const bool first = m_metadata.durationUs == 0;
                m_metadata = result.metadata;
                // At most one completed intermediate preview is published before
                // decoding the newest target; no unbounded seek queue is kept.
                if (!result.frame.image.isNull()) emit frameReady(result.frame.image);
                if (first) synchronizeAudio(true);
            }
        }
        if (m_pending) { m_pending = false; requestFrame(); }
    });
    watcher->setFuture(QtConcurrent::run([worker, path, dimension, musicUs, durationUs] {
        Result result;
        if (!worker->opened) {
            worker->opened = worker->reader.open(path, dimension, &result.error, &worker->canceled);
            if (!worker->opened) return result;
        }
        result.metadata = worker->reader.metadata();
        const auto position = midi_play::visualization::backgroundVideoPosition(
            musicUs, durationUs, result.metadata.durationUs);
        result.frame = worker->reader.frameAt(position, &result.error);
        return result;
    }));
}

void VideoBackgroundController::synchronizeAudio(bool discontinuity)
{
    if (m_audioSynchronizing || m_muted || m_failed || m_audioFailed || !m_metadata.hasAudio || m_path.isEmpty()) return;
    const QScopedValueRollback guard(m_audioSynchronizing, true);
    if (!m_audioPlayer) {
        m_audioOutput = new QAudioOutput(this);
        m_audioPlayer = new QMediaPlayer(this);
        m_audioPlayer->setAudioOutput(m_audioOutput);
        connect(m_audioPlayer, &QMediaPlayer::mediaStatusChanged, this, [this](QMediaPlayer::MediaStatus status) {
            if (status == QMediaPlayer::EndOfMedia) m_audioEndedLoop = m_audioLoop;
            if (status == QMediaPlayer::LoadedMedia)
                synchronizeAudio(true);
        });
        connect(m_audioPlayer, &QMediaPlayer::tracksChanged, this, [this] {
            if (m_audioPlayer->activeVideoTrack() != -1) m_audioPlayer->setActiveVideoTrack(-1);
            if (m_audioPlayer->activeAudioTrack() != 0 && !m_audioPlayer->audioTracks().isEmpty())
                m_audioPlayer->setActiveAudioTrack(0);
        });
        connect(m_audioPlayer, &QMediaPlayer::errorOccurred, this, [this](QMediaPlayer::Error, const QString& error) {
            m_audioFailed = true;
            emit errorOccurred(QStringLiteral("视频原声：%1").arg(error));
        });
    }
    const QUrl source = QUrl::fromLocalFile(m_path);
    if (m_audioPlayer->source() != source) { m_audioPlayer->setSource(source); discontinuity = true; }
    m_audioPlayer->setPlaybackRate(m_rate / 100.0);
    const qint64 videoUs = midi_play::visualization::backgroundVideoPosition(m_musicUs, m_durationUs, m_metadata.durationUs);
    const qint64 loop = m_musicUs / m_metadata.durationUs;
    if (discontinuity || loop != m_audioLoop) m_audioEndedLoop = -1;
    // An audio track may finish before the video stream. Keep the rest of this
    // cycle silent; play() at EndOfMedia would otherwise restart it immediately.
    if (m_audioEndedLoop == loop) return;
    if (!m_playing || m_musicUs >= m_durationUs) m_audioPlayer->pause();
    // The music transport remains authoritative; resync on loop/seek and bound
    // independent device-clock drift. Scrubbing and paused previews stay silent.
    if (discontinuity || loop != m_audioLoop || std::abs(m_audioPlayer->position() - videoUs / 1000) > 120)
        m_audioPlayer->setPosition(videoUs / 1000);
    m_audioLoop = loop;
    if (m_playing && m_musicUs < m_durationUs) m_audioPlayer->play();
}
}
