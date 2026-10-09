#pragma once

// 播放倍率控件：组合按钮、悬浮面板和数值编辑，统一用户输入的提交时机。

#include "domain/settings/playersettings.h"

#include <QTimer>
#include <QToolButton>

class QFrame;
class QSlider;
class QSpinBox;

namespace midi_play::presentation {

// 管理面板和输入生命周期；倍率应用由应用服务负责，模型同步不发出用户编辑信号。
class PlaybackRateControl final : public QToolButton {
    Q_OBJECT
public:
    // 构建面板及延迟隐藏定时器，初始化为领域默认倍率。
    explicit PlaybackRateControl(QWidget* parent = nullptr);
    // 子控件销毁前移除应用级事件过滤器。
    ~PlaybackRateControl() override;

    int ratePercent() const { return m_ratePercent; }

public slots:
    // 规范化并同步按钮、滑块与编辑框；阻断控件信号以避免反馈循环。
    void setRatePercent(int percent);
    // 为按钮和独立工具窗口同步主题样式。
    void setThemeMode(midi_play::settings::ThemeMode mode);

signals:
    // 仅在用户提交不同于已应用值的倍率时发出。
    void ratePercentEdited(int percent);

protected:
    // 悬停显示面板，离开后启动延迟隐藏；过滤器处理外部点击和提交按键。
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    // 在可用屏幕区域内定位面板；点击触发时聚焦数值编辑器。
    void showPanel(bool focusEditor = false);
    // 默认解释并提交文本；取消时恢复已应用倍率。
    void hidePanel(bool commitEditor = true);
    // 焦点、鼠标或拖动仍在交互时保留面板，否则关闭。
    void hidePanelIfInactive();
    // 规范化用户输入并发布变更，重复值直接忽略。
    void applyUserRate(int percent);
    // 以全局坐标判断鼠标是否位于按钮或面板内部。
    bool containsPointer() const;

    QFrame* m_panel = nullptr;
    QSlider* m_slider = nullptr;
    QSpinBox* m_editor = nullptr;
    QTimer m_hideTimer;
    int m_ratePercent = midi_play::settings::kDefaultPlaybackRatePercent;
};

} // namespace midi_play::presentation
