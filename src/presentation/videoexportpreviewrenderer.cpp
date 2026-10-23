#include "videoexportpreviewrenderer.h"
#include <QThread>

namespace midi_play::presentation {

class VideoPreviewWorker final : public QObject {
    Q_OBJECT
public:
    void releaseResources()
    {
#if MIDI_PLAY_HAS_VULKAN
        m_renderer.reset();
        m_rendererCancel.reset();
        m_generation = 0;
#endif
    }

    void render(const visualization::ExportSceneConfig& scene, quint64 generation,
                quint64 revision, qint64 outputUs, qint64 musicUs,
                const std::shared_ptr<std::atomic_bool>& canceled)
    {
        QImage frame;
        QString error;
#if MIDI_PLAY_HAS_VULKAN
        if (!canceled->load()) {
            if (generation != m_generation) {
                m_renderer.reset();
                // VideoFrameReader retains this token's address until renderer destruction.
                m_rendererCancel = canceled;
                auto renderer = std::make_unique<visualization::VulkanOffscreenRenderer>();
                if (renderer->initialize(scene, &error, canceled.get())) {
                    m_renderer = std::move(renderer);
                    m_generation = generation;
                }
            }
            if (m_renderer) m_renderer->render(musicUs, frame, canceled.get(), &error);
        }
#else
        Q_UNUSED(scene) Q_UNUSED(musicUs)
        error = QStringLiteral("当前构建未启用 Vulkan 视频预览");
#endif
        emit completed(generation, revision, outputUs, frame, canceled->load() ? QString() : error);
    }

signals:
    void completed(quint64 generation, quint64 revision, qint64 outputUs,
                   const QImage& frame, const QString& error);

private:
#if MIDI_PLAY_HAS_VULKAN
    quint64 m_generation = 0;
    std::shared_ptr<std::atomic_bool> m_rendererCancel;
    std::unique_ptr<visualization::VulkanOffscreenRenderer> m_renderer;
#endif
};

VideoExportPreviewRenderer::VideoExportPreviewRenderer(QObject* parent)
    : QObject(parent), m_cancel(std::make_shared<std::atomic_bool>(false))
{
}

VideoExportPreviewRenderer::~VideoExportPreviewRenderer()
{
    release();
}

void VideoExportPreviewRenderer::cancel()
{
    m_cancel->store(true);
    ++m_revision;
    m_pending = false;
}

void VideoExportPreviewRenderer::release()
{
    cancel();
    if (m_thread && !m_stopping) {
        m_stopping = true;
        // Serialize cleanup behind any in-flight render; restart only after resources are gone.
        QMetaObject::invokeMethod(m_worker, [worker = m_worker, thread = m_thread] {
            worker->releaseResources();
            thread->quit();
        }, Qt::QueuedConnection);
    }
}

void VideoExportPreviewRenderer::setScene(const visualization::ExportSceneConfig& scene)
{
    cancel();
    m_cancel = std::make_shared<std::atomic_bool>(false);
    m_scene = scene;
    m_scene.outputSize = {1024, 576};
    ++m_sceneGeneration;
}

void VideoExportPreviewRenderer::request(qint64 outputTimeUs, qint64 musicTimeUs, bool discontinuity)
{
    if (m_cancel->load()) return;
    if (discontinuity) ++m_revision;
    m_outputTimeUs = outputTimeUs;
    m_musicTimeUs = musicTimeUs;
    m_pending = true;
    dispatch();
}

void VideoExportPreviewRenderer::dispatch()
{
    if (m_stopping || m_busy || !m_pending || m_cancel->load()) return;
    if (!m_thread) {
        // No parent: the thread outlives a closing dialog until cooperative cancellation finishes.
        m_thread = new QThread;
        m_worker = new VideoPreviewWorker;
        m_worker->moveToThread(m_thread);
        connect(m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
        connect(m_thread, &QThread::finished, m_thread, &QObject::deleteLater);
        connect(m_thread, &QThread::finished, this, [this] {
            m_thread = nullptr;
            m_worker = nullptr;
            m_busy = false;
            m_stopping = false;
            dispatch();
        });
        connect(m_worker, &VideoPreviewWorker::completed, this,
            [this](quint64 generation, quint64 revision, qint64 outputUs,
                   const QImage& frame, const QString& error) {
                m_busy = false;
                if (!m_cancel->load() && generation == m_sceneGeneration && revision == m_revision) {
                    if (!frame.isNull()) emit frameReady(frame, outputUs);
                    else if (!error.isEmpty()) emit failed(error);
                }
                dispatch();
            });
        m_thread->start();
    }
    m_busy = true;
    m_pending = false;
    QMetaObject::invokeMethod(m_worker,
        [worker = m_worker, scene = m_scene, generation = m_sceneGeneration,
         revision = m_revision, outputUs = m_outputTimeUs, musicUs = m_musicTimeUs, canceled = m_cancel] {
            worker->render(scene, generation, revision, outputUs, musicUs, canceled);
        }, Qt::QueuedConnection);
}
} // namespace midi_play::presentation

#include "videoexportpreviewrenderer.moc"
