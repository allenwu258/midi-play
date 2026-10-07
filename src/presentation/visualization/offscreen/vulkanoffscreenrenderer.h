#pragma once
#include "domain/visualization/playbackscenestate.h"
#include <QFont>
#include <QImage>
#include <memory>
#include <atomic>

namespace midi_play::presentation::visualization {
struct ExportSceneConfig {
    midi_play::visualization::PlaybackSceneState state;
    QSize logicalSize {1280, 720};
    QSize outputSize {1920, 1080};
    QFont font;
    QImage background;
};

// A submitted frame owns one slot in the renderer until completeRender().
// The ticket is intentionally small; it only carries the slot identity
// needed to match a GPU completion with its frame.
struct VulkanRenderTicket {
    int slot = -1;
    qint64 musicPositionUs = 0;

    bool valid() const { return slot >= 0; }
};

// Owns a Vulkan device and a bounded set of image targets. No window, surface
// or swapchain is involved. beginRender()/completeRender() expose GPU work
// without making callers share Vulkan objects across threads.
class VulkanOffscreenRenderer final {
public:
    VulkanOffscreenRenderer();
    ~VulkanOffscreenRenderer();
    bool initialize(const ExportSceneConfig& config, QString* error);
    bool beginRender(qint64 musicPositionUs, VulkanRenderTicket& ticket,
                     const std::atomic_bool* canceled, QString* error);
    bool completeRender(VulkanRenderTicket& ticket, QImage& frame,
                        const std::atomic_bool* canceled, QString* error);
    bool render(qint64 musicPositionUs, QImage& frame, const std::atomic_bool* canceled, QString* error);
private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
};
} // namespace midi_play::presentation::visualization
