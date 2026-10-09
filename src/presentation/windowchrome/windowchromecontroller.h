#pragma once

// 窗口装饰控制器：封装平台原生框架、命中测试、标题栏按钮输入和系统命令。

#include "domain/settings/titlebarmode.h"
#include "domain/settings/thememode.h"

#include <QObject>
#include <QPointer>
#include <QVector>
#include <memory>

class QWidget;

namespace midi_play::presentation::windowchrome {
class CaptionButtons;

// 在 GUI 线程管理原生框架，将 Win32 类型和消息隔离在实现中；其他平台保留 Qt 原生装饰。
class WindowChromeController final : public QObject {
    Q_OBJECT
public:
    // 以主窗口为 Qt 父对象，安装窗口事件过滤器并创建私有原生状态。
    explicit WindowChromeController(QWidget* window);
    ~WindowChromeController() override;

    // 注册标题区、系统菜单入口和不可拖动的操作控件，并安排框架刷新。
    void setTitleBar(QWidget* titleBar, QWidget* systemMenuWidget,
                     const QVector<QWidget*>& interactiveWidgets);
    // 接入 Qt 标题栏按钮命令，替换时断开旧按钮连接。
    void setCaptionButtons(CaptionButtons* buttons);
    // 规范化模式后同步应用，原生集成失败会回退并发布有效模式。
    void setMode(midi_play::settings::TitleBarMode mode);
    // 同步按钮与已应用的 DWM 外观。
    void setTheme(midi_play::settings::ThemeMode theme);
    midi_play::settings::TitleBarMode mode() const noexcept { return m_mode; }
    // 返回转换为 Qt 逻辑像素后的系统按钮宽度和标题区最小高度。
    int captionButtonsWidth() const noexcept;
    int minimumCaptionHeight() const noexcept;

    // 处理本窗口的受支持原生消息；返回 true 表示结果已写入且消息被接管。
    bool processNativeEvent(const QByteArray& eventType, void* message, qintptr* result);
    // Windows 有有效句柄时投递系统命令，否则使用 QWidget 的对应窗口操作。
    void minimize();
    void maximizeOrRestore();
    void close();
    // 在系统菜单图标下方打开 Windows 系统菜单，全屏或无有效句柄时忽略。
    void showSystemMenu();

signals:
    // 通知主窗口重新布局；失败信号附带回退原生标题栏的具体原因。
    void frameMetricsChanged();
    void modeChanged(midi_play::settings::TitleBarMode mode);
    void nativeIntegrationFailed(const QString& reason);

protected:
    // 句柄、窗口状态、屏幕和 DPI 变化时延迟刷新，激活变化仅同步按钮外观。
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    // 平台细节、原生句柄、DWM 原始属性及按钮捕获状态均保存在私有实现中。
    struct NativeFrame;
    // 应用框架；失败时恢复原始属性并切回原生模式。
    void refreshFrame();
    // 合并同一事件循环内的多次刷新请求，避免重入布局和句柄更新。
    void scheduleRefresh();

    // 关联控件均为弱指针，防止 Qt 销毁窗口或替换顶栏后留下悬空引用。
    QPointer<QWidget> m_window;
    QPointer<QWidget> m_titleBar;
    QPointer<QWidget> m_systemMenuWidget;
    QPointer<CaptionButtons> m_captionButtons;
    QVector<QPointer<QWidget>> m_interactiveWidgets;
    midi_play::settings::TitleBarMode m_mode = midi_play::settings::TitleBarMode::Native;
    midi_play::settings::ThemeMode m_theme = midi_play::settings::kDefaultThemeMode;
    bool m_refreshPending = false;
    std::unique_ptr<NativeFrame> m_native;
};

} // namespace midi_play::presentation::windowchrome
