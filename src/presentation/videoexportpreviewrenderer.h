#pragma once

#include "visualization/offscreen/vulkanoffscreenrenderer.h"
#include <QObject>
#include <atomic>
#include <memory>

class QThread;

namespace midi_play::presentation {
class VideoPreviewWorker;

// GUI-thread coordinator: one in-flight render and one replaceable pending request.
class VideoExportPreviewRenderer final : public QObject {
    Q_OBJECT
public:
    explicit VideoExportPreviewRenderer(QObject* parent = nullptr);
    ~VideoExportPreviewRenderer() override;
    void setScene(const visualization::ExportSceneConfig& scene);
    void request(qint64 outputTimeUs, qint64 musicTimeUs, bool discontinuity);
    void cancel();
    // Cancel immediately; release GPU/decoder resources asynchronously before restarting.
    void release();
    bool hasWorkerThread() const noexcept { return m_thread != nullptr; }

signals:
    void frameReady(const QImage& frame, qint64 outputTimeUs);
    void failed(const QString& error);

private:
    void dispatch();
    visualization::ExportSceneConfig m_scene;
    QThread* m_thread = nullptr;
    VideoPreviewWorker* m_worker = nullptr;
    quint64 m_sceneGeneration = 0;
    quint64 m_revision = 0;
    qint64 m_outputTimeUs = 0;
    qint64 m_musicTimeUs = 0;
    bool m_busy = false;
    bool m_pending = false;
    bool m_stopping = false;
    std::shared_ptr<std::atomic_bool> m_cancel;
};
} // namespace midi_play::presentation
