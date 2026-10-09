#pragma once

// 音符动态、演奏法和表达曲线到后端事件的渲染接口。
#include "playbackcontext.h"
#include "playbacktypes.h"

#include <memory>

namespace midi_play::playback {

// 将模型中的音符事件渲染为包含力度、演奏法和表达曲线的后端事件。
class PlaybackEventsRenderer final {
public:
    // 使用预计算的动态上下文构造渲染器。
    explicit PlaybackEventsRenderer(std::shared_ptr<const PlaybackContext> context);
    // 返回渲染后的事件；源 PlaybackData 不会被修改。
    QVector<PlaybackEvent> render(const PlaybackData& source) const;

private:
    std::shared_ptr<const PlaybackContext> m_context;
};

} // namespace midi_play::playback
