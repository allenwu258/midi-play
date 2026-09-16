# 深色 / 浅色主题开发方案

状态：功能已实现，Windows Release 验证及发行包已完成；实际验收记录见第 12 节。

基线：`dc98e0a`（简谱条显隐、持久化及黄线联动隐藏已提交）。

工作分支：`feat/light-dark-themes`，从上述提交创建。

## 1. 产品行为与边界

- 设置窗口新增“界面主题”下拉选择，包含“深色”“浅色”。首次运行及缺少配置的旧用户保持深色。
- 选择后立即更新主窗口、已打开的设置窗口、倍速按钮与浮窗、自绘图标、下落区域、琴键、文本及动画；自动保存，重启恢复。
- 主题与图形模式、简谱条显隐、标题栏样式分别存储。切换 Qt / Vulkan、打开新乐曲、暂停、停止、seek 后保持主题。
- 简谱条隐藏时，两套主题都不显示黄线；音符继续在琴键顶部判定。
- 保留现有方角音符、简洁布局、音高与轨道色彩身份。主题不改变字体尺寸、控件尺寸、布局、音乐时间或音符几何。
- 本期提供手动选择的两套内置主题。系统自动跟随、定时切换、自定义主题文件、主题编辑器、主题过渡动画不进入本期。
- 默认即时切换，不做全屏渐变，以免高密度播放时引入额外绘制及视觉拖影。
- 操作系统原生文件选择器遵循平台支持的外观能力；应用自身窗口必须完整覆盖，原生标题栏优先使用 Qt 6.8 的颜色方案请求。

## 2. 现状及技术影响

| 位置 | 已有实现 | 主题化需要处理的问题 |
| --- | --- | --- |
| `src/presentation/mainwindow.cpp` | 主窗口 QSS、播放控制、窗口按钮图标、标准图标 | QSS 和预先栅格化图标都写死颜色，仅设置 QApplication palette 无法覆盖 |
| `src/presentation/settings/settingsdialog.cpp` | 独立非模态窗口，拥有局部 QSS | 必须更新已经打开的窗口，并处理下拉列表、复选框、输入框和禁用状态 |
| `src/presentation/transport/playbackratecontrol.cpp` | 局部 QSS，`Qt::Tool` 倍速浮窗 | 浮窗应显式更新，保留输入编辑、焦点和鼠标交互，不能销毁重建 |
| `src/presentation/windowchrome` | 自定义标题栏及拖动、缩放行为 | 颜色更新与标题栏模式更新分开，主题切换不能触发窗口隐藏或原生句柄重建 |
| `fallingnotesrenderer.h` | `VisualizationTheme` 只有一套默认深色 | 抽离为共享主题定义，补齐遗漏的画布语义颜色 |
| `fallingnotesrenderer.cpp` / `vulkanscene.cpp` | 音高背景、鼓键、文字、遮罩、颤音标记仍有硬编码 | 两个后端必须消费同源参数，不各自维护另一套颜色 |
| `notematerial.cpp` | OKLab 色彩计算，明度及 alpha 针对深色画布 | 浅色画布直接复用会使音符头、尾和高光发白，必须设计独立材质参数 |
| `noterendercache.cpp` | 材质主要随 chart 重建，横向几何随尺寸重建 | 增加独立材质版本；同 chart、同尺寸换主题也必须更新颜色 |
| `noterastercache.cpp` | 按 chart、geometry、DPR 管理带颜色的纹理缓存 | 增加材质版本，否则 Qt 音符可能继续使用旧主题纹理 |
| `fallingnotesvulkanwindow.cpp` | 清屏色写死；每张交换链图像持有独立资源 | 清屏、静态 UI、音符实例和动态 UI 必须在同一场景主题下生成 |
| `shaders/note.vert` | 颤音标记仍强制白色 | 将颜色改为 CPU 主题数据提供，尽量复用现有实例字段 |
| `src/domain/visualization/playbackvisualizationprojector.cpp` | 产生稳定轨道基础色及音乐派生数据 | 保留主题无关；换主题不重新投影、解析或构建可见音符索引 |
| `src/cli/main.cpp` | `--render-test` 使用固定默认场景 | 增加显式主题参数，便于可重复截图，不隐式读取用户配置 |

实际应用使用 Qt 6.8.3，本地 SDK 已确认 `QStyleHints::setColorScheme()` 可用。该接口是平台外观请求，不能代替 QSS、自绘图标或 Vulkan 配色。

## 3. 分层与模块设计

### 3.1 设置层只保存主题身份

新增 `src/domain/settings/thememode.h`：

- `enum class ThemeMode : int { Dark = 0, Light = 1 };`
- `kDefaultThemeMode = ThemeMode::Dark`。
- 有效值判断、持久化值转换和枚举归一化函数；非法整数回退深色。
- 元类型声明供信号和槽使用，不在该文件放 QColor 或样式字符串。

`PlayerSettings` 增加 `themeMode`，schema 从 5 升到 6。

持久化键为 `General/themeMode`，类型 int，默认 0。读取使用 `toInt(&ok)` 并验证值域；缺失键使用默认值，格式错误或未知值回退深色并记录告警。加载过程不为了修复单个键立即重写整份配置。保存其他选项时保留当前主题。

`SettingsService` 增加 getter、`setThemeMode()`、`themeModeChanged()`。相同有效值不重复发信号、不重复写盘。保存失败沿用现有错误提示机制：本次界面选择仍有效，但明确告知无法保存，不能把临时成功显示当成持久化成功。

注意：当前启动时先 `load()`，之后才创建窗口并连接加载告警信号。若主题非法配置需要可见告警，应让 SettingsService 保留最后一次加载告警，主窗口初始化读取，避免启动告警在连接前丢失。

### 3.2 展示层维护两份不可变主题定义

建议新增 `src/presentation/theme/`：

| 文件 | 职责 |
| --- | --- |
| `apptheme.h/.cpp` | `AppTheme` 及两套只读定义；`themeFor(ThemeMode)` 为唯一查询入口 |
| `widgetstyles.h/.cpp` | 从主题角色生成主窗口、设置、倍速控件及浮窗的样式；尺寸规则共用 |
| `themecontroller.h/.cpp` | GUI 线程上的主题应用协调，维护当前模式，应用 QApplication palette、平台颜色方案，广播更新 |
| `themeicons.h/.cpp` | 集中生成现有自绘按钮图标，处理 Normal / Active / Disabled / Selected 及 DPR |

`AppTheme` 包含明确的数据组：

- 控件：窗口、面板、输入区、浮窗、普通/次要/禁用文本、边框、hover、pressed、focus、selection。
- 功能状态：播放、暂停、停止、节拍器关闭/开启/禁用、错误及警告；保持原有功能含义。
- 可视化：画布、音高阴影、拍线、小节线、简谱文字、判定线及光带、白键、黑键、鼓键、琴键文字、歌词、加载与错误遮罩、颤音标记。
- 音符材质：明度、色度、alpha、琴键着色与击键光效参数。
- 图标：普通、悬停、禁用、强调底色上的前景色，以及关闭按钮红色背景上的前景色。

不把业务组件的所有颜色都压缩成 `primary/secondary` 两三个泛化字段。文字、边框、填充、强调背景上的文字要独立，避免一处调色同时破坏多个状态。

将 `VisualizationTheme` 从 Qt 渲染器头文件移到共享主题定义，Vulkan 不再通过引用 Qt 渲染器头文件获取主题类型。

### 3.3 初始化与运行时传播

启动顺序：

1. 创建 QApplication，设置应用标识，加载 SettingsService。
2. 创建 ThemeController，用已保存的 ThemeMode 应用平台颜色方案和应用 palette。
3. 创建 MainWindow 并注入 ThemeController；构造期间先应用当前主题，再显示窗口。
4. MainWindow 将同一主题传给图标、倍速控件和 FallingNotesView。后续首次创建设置窗口或 Vulkan 子窗口时使用当前主题初始化。

运行时顺序：设置下拉框 → SettingsService → ThemeController → 各消费者。

- SettingsService 是持久化偏好的唯一来源；ThemeController 不另存一份配置、不持有播放服务。
- ThemeController 切换时先应用 Qt 平台/基础 palette，再发主题更新信号。QSS 与显式自绘颜色仍来自 AppTheme；不从系统当前 palette 反推应用主题。
- MainWindow 统一更新主窗口样式、已持有图标、倍速控件及画布；SettingsDialog 自身订阅主题更新。
- 设置下拉框使用 `QSignalBlocker` 同步选择，避免程序更新控件引发再次保存。
- 全链路在 GUI 线程同步更新主题状态，最后请求重绘；不调用 `processEvents()`，不在主题 setter 中抓图或等待 GPU。
- 控件初次创建显式应用当前主题；不依赖“过去某次信号是否收到”。

## 4. 深浅视觉规范

### 4.1 基础配色方向

以下是实现起点，浅色最终数值要以实际音乐截图和对比度校验确定，不视作已验证的最终视觉稿。

| 角色 | 深色方向 | 浅色候选 |
| --- | --- | --- |
| 画布 | 保留 `#121416` | 微灰绿 `#F3F5F2`，避免大面积刺眼纯白 |
| 顶部、底部及设置面板 | 保留 `#1B1D20` | `#FFFFFF` / `#EEF1ED` 分层 |
| 主文本 | 保留 `#F0F1ED` | `#202723` |
| 次要文本 | 延续现有灰绿 | `#606B65` |
| 输入控件 | 保留深灰 | `#E8EDE8`，与面板分离 |
| 装饰分隔线 | 延续现有灰线 | `#CCD4CD`；交互边界及焦点用更深颜色 |
| 操作强调色 | 延续现有青绿体系 | 深青绿 `#176B56`，保证强调色上的文字清楚 |
| 白键 / 黑键 | 保留现状 | `#FFFEFA` / `#2B302D`，维持钢琴黑白语义 |
| 可见简谱条的判定线 | 保留金黄 `#F4D35E` | 较深的金色，如 `#A57A17` |
| 拍线 / 音高阴影 | 浅色低透明度 | 深灰绿低透明度，重新控制层级 |

普通正文与背景对比度目标至少 4.5:1，大文字及关键控件图形目标至少 3:1。禁用状态和装饰网格单独评估；半透明绘制应验证混合后的颜色，而非只比较 RGB 源值。

两套主题使用同样的字体、间距、边框宽度和控件高度。应用基础 palette 覆盖 Active / Inactive / Disabled、Base / AlternateBase、Button、Text、Highlight、HighlightedText、ToolTip 等角色，补齐未被局部 QSS 覆盖的控件状态。

### 4.2 音符材质

继续使用现有 OKLab 与色域收敛算法，保留轨道 hue、音区微调、力度分桶、声部差异、ghost 语义。

- 深色 profile 先严格沿用现有材质数值，作为回归基线。
- 浅色 body 使用更低明度和适当更高的不透明度，避免白底上“淡色玻璃”的低对比；head 采用同色相的较深强调边，tail 保持低权重。
- 浅色 body 的初始调参范围可取 OKLab L 约 0.50–0.62、alpha 约 0.60–0.80；head 更深且更实；这些范围需覆盖全部轨道色相、低力度和密集叠加后再确定。
- 材质继续分为 head / body / pedal tail，尾部宽度、方角、音符长度和起止时间完全一致。
- 将画布音符头色与琴键高光解耦。浅色主题的深色 head 不应直接成为黑键顶部高光，否则黑键按下状态可能不明显。
- 建议在 NoteMaterial 中增加 `keyFill`、`keyTop` 和 `glow`，分别用于琴键混色、琴键顶部强调及击键光效。深色分别沿用原 body、head、head，保持现有行为；浅色独立调参。必要时在主题 profile 中区分白键与黑键混色强度。
- 击键效果保留位置和节奏：深色呈柔和发光，浅色呈克制的彩色光晕。避免加重全屏 glow 或提高所有图层 alpha 来补偿对比度。
- 现有密度补偿根据可见面积调整 bodyOpacity。先保留其公式，通过浅色密集样本检查可读性；确需差异时只把补偿参数放入共享 profile，不能在两个后端分别修补。

共享接口调整为 `makeNoteMaterial(chart, note, profile)`；音乐投影层不接受主题参数。

## 5. 缓存失效与 Qt 渲染

这是本功能最重要的正确性边界之一：主题切换只使带颜色的数据失效。

| 数据 | 换主题时的处理 |
| --- | --- |
| MusicDocument、VisualChart、音乐时钟、音频会话 | 保留，不重新分析或投影 |
| VisibleNoteIndex、候选音符时间窗口 | 保留 |
| 音符横向几何、SceneLayoutEngine 输出 | 保留，主题不能改变判定位置 |
| NoteRenderCache 材质及渐变画刷 | 重建，递增独立 `materialRevision` |
| NoteRasterCache 带颜色的横向纹理 | 根据 materialRevision 失效并按需重新生成 |
| Qt 静态键盘图像 | 标记 dirty，下一帧生成新主题图像 |
| 击键高亮、光效、遮罩 | 下一帧按新 profile 生成，保留原 effectsStartUs |
| 文字排版与 Vulkan 字形 alpha 图集 | 主题仅改颜色时保留；字体、DPR 变化按已有规则处理 |

NoteRenderCache 当前将材料构建集中在 chart 重建中。应抽出样式构建逻辑并保留每种去重样式的代表音符索引，换主题只重新计算样式，而不是重新遍历并构造全部音符时序和位置数据。`noteMaterialKey` 继续描述音符身份，主题身份由缓存外层单独管理。

NoteRasterCache 的命中条件增加 materialRevision；不能只清空渲染器外壳而遗漏纹理缓存。

FallingNotesView 新增 `setThemeMode()`，更新渲染器主题、静态琴键 dirty 状态，并向已存在的 Vulkan 窗口传递。主题 setter 不设置 geometryDirty，不重置播放时钟或瞬态效果。

Qt 画布还要迁移目前散落的音高背景白色 alpha、鼓键背景/边框、琴键文字、颤音线、加载与错误遮罩颜色。中性白色 alpha-mask、透明清空等算法用途不属于主题硬编码。

## 6. Vulkan 渲染与同步

### 6.1 完整传递主题

- PlaybackSceneState 增加轻量的 ThemeMode（领域层只引用主题枚举，不引用 QColor/AppTheme）；窗口 `sceneState()` 携带当前选择。
- VulkanScene::prepare 明确区分 chartChanged、geometryChanged、themeChanged、materialChanged。
- themeChanged 触发静态 UI 颜色和音符实例颜色更新，递增对应 revision；不能只更换 `m_theme` 留下已缓存的顶点颜色。
- 动态 UI、网格、标签及击键效果从本帧主题生成。
- VkClearValue 使用本帧场景的 background，移除固定 `{18/255,20/255,22/255}`。准备失败的降级清屏也应使用当前请求的主题，避免浅色窗口露出深色底。
- shader 中颤音的固定白色改为接收实例颜色：CPU 为 kind=4 填入主题色，vertex shader 保留传入 fill。优先复用现有 VulkanQuad 字段，保持 112 字节 ABI 和 push constants 布局不变。
- 保持 straight-alpha → premultiplied-alpha 只转换一次；本期不改变 swapchain 色彩空间或混合公式，否则会扩大双后端色差和兼容性风险。

### 6.2 保持已修复的资源生命周期

继续遵守 `docs/vulkan-rendering.md`：

1. 每张交换链图像各自拥有静态 UI、动态 UI、音符、图集和 staging。
2. 主题更改先更新 GUI 线程上的场景状态；进入该图像安全的 startNextFrame 时，按 revision 更新它自己的资源。
3. 一帧的 clear、静态 UI、音符和动态 UI 使用同一份主题。可以在切换边界呈现已经提交的完整旧帧，但不能在一帧中拼出不同主题。
4. 不在设置槽里直接写 GPU buffer，不切回 currentFrame 索引，不删除 completePresentation 兼容等待。
5. 不重建 QVulkanWindow、device、pipeline、render pass 或 swapchain 来实现换色。
6. 同主题重复设置不递增 revision；稳定播放时没有主题导致的重复静态上传。

字形图集当前提供白色 alpha mask，文字实际颜色来自实例，因此颜色变化不要求重新上传整张 2048×2048 图集。保留字体与 DPR 改变时的正常回收策略。

## 7. 控件、图标及窗口外观

- 各组件通过 `applyTheme(const AppTheme&)` 更新已有控件，不销毁窗口、不重连播放相关信号、不重设滑块/编辑器值。
- QSS 生成函数集中管理颜色，布局常量复用。清除旧局部硬编码 QSS，避免父层浅色被子层深色样式覆盖。
- 限定 selector 的 objectName 或组件范围；不要用全局 `QWidget { background: ... }` 覆盖可视化画布或全部子控件。
- 倍速浮窗显式设置 palette 和样式，切换时保留尚未提交的输入、滑块位置、浮窗位置及隐藏计时器状态。
- 自绘 QPixmap 图标需要重新生成或通过 QIconEngine 按当前主题绘制，不能假定修改文字色会改变已有图标。
- 图标需覆盖 hover、disabled、pressed 和选中状态；浅色关闭按钮在红色 hover 背景上仍使用清晰前景。普通前景色不必与播放等强调按钮上的图标色相同。
- 当前“打开乐曲”“设置”按钮为构造局部变量。若采用重新生成图标的方案，应将它们变为成员引用，避免用 findChildren 扫描猜测目标。
- 主题不改变 QStyle 实例。优先保留现有平台 style，以 QPalette 和明确 QSS 控制颜色，避免运行时切换 style 导致尺寸、焦点和弹出层行为变化。
- 使用 `QStyleHints::setColorScheme(Dark/Light)` 请求原生窗口外观。先验证 Windows 主窗口及设置窗口标题栏；平台能力不足时，必要的 DWM 适配封装在 windowchrome，检查返回值，仅操作已存在窗口句柄，并在 WinIdChange/显示时重新应用。
- 原生系统文件窗口可能遵循操作系统主题，作为平台边界记录；不为统一颜色强行替换其文件选择体验。

## 8. 设置交互与可重复诊断

“界面主题”放在设置窗口的视觉配置区域，与图形模式、简谱条选项相邻。只有两个清晰选项，说明“修改立即生效并自动保存”。主题切换无需按钮确认或重启。

CLI 的 `--render-test` 增加可选命名参数 `--theme dark|light`，保留现有位置参数语法兼容。未指定时固定 dark，非法值返回参数错误，不读取个人 settings.ini，确保 CI 可复现。

截图及 native GPU 测试扩展主题参数，输出目录按主题和简谱条显隐区分。更新 README、配置表和 Vulkan 文档，记录 themeChanged 会触发静态颜色更新的新条件。

## 9. 实施顺序与交付物

| 阶段 | 工作 | 完成标准 |
| --- | --- | --- |
| A：主题基础与设置 | ThemeMode、两套 AppTheme、严格持久化、SettingsService、ThemeController | 旧配置保持深色；选择可保存；应用首帧使用已保存主题 |
| B：完整 Widgets 外观 | 主窗口、设置、倍速浮窗、基础 palette、图标及原生标题栏请求 | 两套主题的交互状态清晰，窗口与控件状态保持 |
| C：共享材质与 Qt 画布 | 材质 profile、材质 revision、纹理失效、静态键盘及全部画布颜色 | 同尺寸同乐曲往返切换无旧色；暗色基线保持；浅色可读 |
| D：Vulkan 接入 | 清屏、静态/动态颜色、实例数据、颤音 shader、per-image revision | 与 Qt 对齐；不重建窗口；无同步校验错误及键盘闪烁 |
| E：回归与发布包 | CLI 参数、自动测试、视觉校验、连续 GPU 测试、文档与打包 | 完整 Release 包可独立运行，附测试及已知平台限制 |

A–D 作为同一功能完整交付；中间的半主题画面不作为用户可执行成品。

构建接入时同步更新 CMake 公共源码清单，以及单独枚举领域/渲染源码的测试目标；不能只让主程序通过编译而遗漏测试链接依赖。新增模块复用现有 Qt/C++20 能力，无需增加第三方依赖。

## 10. 验证计划

### 设置与程序生命周期

- 缺失配置、schema=5 旧配置、dark/light 重启、错误字符串、未知枚举、写入失败。
- 主题保存后修改刷新率、音源、图形模式及简谱条，不覆盖主题；反向同样验证。
- 相同值重复设置不产生额外保存或主题广播。
- 已打开设置窗口、新打开窗口、已打开倍速浮窗均更新；复选框、下拉选中项和输入文本清晰。
- 主题切换不触发音频重新加载/seek/停止，不改变播放速率、节拍器、乐曲或音源；用应用层信号与音频替身确认。

### 渲染与缓存

- 核心组合：2 主题 × 2 图形模式 × 2 简谱条状态，共 8 组。
- 每组检查空场景、暂停、播放、加载和错误；补充鼓组、踏板尾部、低力度、ghost、颤音、歌词及稠密和弦。
- 固定 chart、尺寸、时间，执行 dark→light→dark；每次都与新建渲染器的对应结果比较，查找旧主题缓存。
- 断言换主题更新 materialRevision 和需要的 UI/notes revision，但不更新音乐投影、索引及横向几何版本；字体与 DPR 不变时保留 atlas revision。
- 两个后端对比既看整体误差，也看文字、音符头、尾、琴键内部、无音符背景等局部区域，避免细小关键元素被全图平均掩盖。
- 简谱隐藏必须明确断言黄线与黄光带不存在，显示时恢复；该检查不能只依赖后端彼此一致。
- 在最小窗口和常用尺寸、DPR 1.0/1.5/2.0 下检查中文文本、图标、边界及浮窗。

### Vulkan 连续呈现与性能

- 4096 音符压力场景持续呈现，期间反复换主题、简谱显隐、seek 和 resize。
- 使用用户提供的《如愿》MIDI 覆盖完整时间线；采样未按下琴键的稳定性。
- 启用资源 completion event 审计和 synchronization validation，要求 0 错误。
- 连续呈现不能全程依赖 grab()；grab 会等待 GPU，可能掩盖资源生命周期错误。
- 记录稳定播放与主题切换帧的 p50/p95、缓存重建计数和上传量，与改动前同一设备同一场景比较；不以单次平均 FPS 声称无性能回归。
- 验证主题重复切换后内存趋于稳定；如果样式重建在极端大曲目上过重，再考虑按需重建材质，不提前引入后台渲染线程。

### 构建及打包

- 原有全部 CTest 通过，并增加有意义的主题设置/缓存/视觉回归；不为纯颜色常量编写镜像实现的测试。
- Vulkan 开启和 Traditional-only 两种构建均通过，公共主题模块不得依赖 Vulkan SDK。
- Windows 原生/自定义标题栏及系统深浅外观交叉检查；macOS/Linux 没有运行验证时明确记录。
- Release 包执行移除 SDK PATH 后的依赖闭包和渲染检查，输出独立目录 `dist/midi-play-themes`，保留现有可执行包。

## 11. 主要风险及控制

| 风险 | 用户可见表现 | 控制方式 |
| --- | --- | --- |
| 只改窗口背景 | 设置、浮窗、图标仍深色或看不清 | 完整控件和绘制入口清单，覆盖所有状态 |
| 音符复用深色材质 | 浅色尾部、头部融入背景 | 独立 profile，基于实际合成结果校验 |
| 材质缓存遗漏 | 背景已浅色，音符仍旧色 | 独立 materialRevision，往返切换对比新渲染器 |
| Vulkan 静态批次未刷新 | 键盘或音高背景停留在旧主题 | themeChanged 纳入静态/音符 revision，per-image 延迟安全上传 |
| Vulkan 清屏色遗漏 | 空白区域或过渡时露出黑色 | clear 与 scene 使用同一主题 |
| 图集被无谓重建 | 切换瞬间大量上传、帧时间尖峰 | 保留主题无关 alpha atlas，仅更新文字实例颜色 |
| 换主题重建窗口 | 音画抖动、焦点丢失、输入中断 | 原位 applyTheme，不调用标题栏模式切换路径 |
| 原生 style 及 OS 外观干扰 | 控件状态与显式选择不一致 | 明确平台 scheme + 全套 palette + 局部 QSS，测试 OS 相反主题 |
| 新增颜色散回业务文件 | 两主题维护成本持续上升 | 所有主题角色集中定义，渲染器只消费语义字段 |

主题功能的最终验收是：可保存的选择、完整协调的两套界面、清晰的音符层级、正确的缓存更新，以及稳定的连续播放。这些需要同时完成。

## 12. 实施及验收记录（2026-09-16）

本分支已实现上述主题模块、设置持久化、运行时切换、双后端材质和缓存更新，以及 CLI 主题参数。主题代码未提交，基线提交不变。

实际实现使用 `ThemeController::themeChanged(ThemeMode)` 传递主题身份，各控件通过 `themeFor()` 查询不可变定义。浅色音符采用深色强调边与独立的琴键高亮参数；深色继续使用原有音符材质。主窗口、设置、浮窗和图标共用语义色，音乐投影和音频层不依赖展示层主题。

加载告警保存在 `SettingsService` 中，主窗口初始化及设置窗口都可读取，避免启动时信号尚未连接而丢失提示。写入失败保留本次外观并在设置中显示错误。

| 验证 | 实际结果 |
| --- | --- |
| Vulkan Release / Traditional-only Release | 两种构建均成功，各 8 项 CTest 全部通过 |
| 配置 | 缺失键、schema=5、非法整数/字符串、重启恢复、重复选择、保存失败通过 |
| 运行时交互 | 已有与新开设置窗口、倍速待确认输入及焦点保持、原生/自定义窗口句柄保持通过 |
| CPU 缓存 | DPR 1.0 / 1.5 / 2.0、简谱显示/隐藏、dark→light→dark 与新渲染器结果一致；材质变化不重建 chart 或横向几何 |
| GPU 缓存 | 主题更新静态 UI、音符及动态颜色；同主题不重复更新静态版本，字体图集与判定位置保持 |
| 双后端画面对比 | 2 主题 × 2 简谱状态，Qt/Vulkan 下落区域平均通道差 0.597～0.819 / 255，琴键内部差 0.005～0.010 / 255 |
| 4096 音符压力场景 | 900 帧以上连续呈现中交替切换主题与简谱状态；随后 24 帧琴键读回检查通过，validation error 为 0 |
| 《如愿》MIDI | 1373 个音符，覆盖完整约 260 秒时间线，1041 个时间采样、5064 次呈现；琴键检查通过，validation error 为 0 |
| 深色基线 | 《如愿》40 秒、1280×720 的 CLI 输出与修改前发行包 PNG 的 SHA256 一致 |
| 外观检查 | 检查了两套主窗口、设置、倍速面板、浅色最小窗口、自定义标题栏及两种简谱布局截图 |
| 发行包 | 清除 SDK PATH 后的依赖闭包、CLI 与渲染检查通过；显式 `--theme dark/light` 可用，非法主题返回 2 |

测试环境：Windows x64、Qt 6.8.3、MSVC 2022、RTX 5060 Laptop GPU，桌面 DPR 1.5。GPU 连续阶段没有逐帧调用 `grab()`；资源完成事件审计和 synchronization validation 均启用。普通 CTest 不要求可用的 Vulkan 显卡。

可复现入口（开发终端需要设置 Qt DLL 与插件路径）：

```powershell
.\build\visual-polish-release\Release\midi_play_theme_tests.exe --snapshots build/theme-diagnostics
.\build\visual-polish-release\Release\midi_play_note_visual_tests.exe --snapshots build/theme-diagnostics --vulkan
.\build\visual-polish-release\Release\midi_play_presentation_tests.exe --vulkan-smoke
$env:VK_LAYER_VALIDATE_SYNC = '1'
.\build\visual-polish-release\Release\midi_play_note_visual_tests.exe --vulkan-stress
.\build\visual-polish-release\Release\midi_play_note_visual_tests.exe --vulkan-stress --midi 'C:\path\reproducer.mid'
```

交付入口：`dist/midi-play-themes/midi_play.exe`。运行或分发时保留同目录 DLL、插件及 `assets`，旧的发行目录保留。

验证边界：未在 macOS/Linux 或其他显卡上实机验证；Windows 原生外观通过 Qt 的颜色方案接口请求，操作系统原生文件窗口仍受平台支持影响。没有进行独立的改动前后帧时间及长期内存对照实验，因此上述结果不作为跨设备性能承诺。
