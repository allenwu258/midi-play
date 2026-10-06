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

// Owns a Vulkan device and an image target. No window, surface or swapchain.
class VulkanOffscreenRenderer final {
public:
    VulkanOffscreenRenderer();
    ~VulkanOffscreenRenderer();
    bool initialize(const ExportSceneConfig& config, QString* error);
    bool render(qint64 musicPositionUs, QImage& frame, const std::atomic_bool* canceled, QString* error);
private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
};
} // namespace midi_play::presentation::visualization
