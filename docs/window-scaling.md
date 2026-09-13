# 窗口缩放

窗口模式下可拖动四边或四角调整大小，默认保持内部渲染画面的宽高比。使用 1280×720 渲染时，即保持 16:9；窗口尺寸可以大于或小于内部渲染尺寸。

支持标题栏最大化按钮。最大化时维持带标题栏的窗口模式，在当前显示器工作区内按比例居中放大；还原后回到之前的窗口位置和尺寸。最大化命令绕过游戏原生的切换全屏分支。

`config.ini` 的 `[general]` 示例：

```ini
WindowedMode=true
width=2560
height=1440
render_width=1280
render_height=720
resizable_window=true
keep_aspect_ratio=true
```

- `width`、`height` 是客户区尺寸，不含标题栏和边框。正常退出时更新，下次启动恢复。
- `render_width`、`render_height` 是游戏内部渲染尺寸。拖动窗口不改变地图视野或 UI 的逻辑布局。
- `resizable_window` 默认开启；关闭后不添加可拖动边框。
- `keep_aspect_ratio` 默认开启；关闭后允许独立调整宽高并拉伸画面。
- `enable_scaling=false` 会关闭这套窗口缩放功能。
- UAC / 设备恢复时保留当前窗口尺寸。从全屏按 Alt+Enter 切回窗口时，重新读取配置中的 `width`、`height`，恢复普通窗口。

退出时在 `[general]` 中自动保存 `window_x`、`window_y`（窗口外框左上角的屏幕坐标）、`window_maximized`（最大化状态），以及 `window_restore_width`、`window_restore_height`、`window_restore_x`、`window_restore_y`（最大化前的普通窗口，用于下次还原）。首次没有坐标配置时按默认位置打开；保存的显示器已断开、窗口完全在屏幕外时，将位置移回可用显示器。

最小化或全屏状态下正常退出，保存最后一个有效窗口布局。关闭窗口、系统结束会话和游戏直接调用 `ExitProcess` 均会保存。配置采用同目录临时文件替换，只更新窗口相关字段；如果原来没有显式 `render_width` / `render_height`，会补齐当前渲染基准，避免保存后的窗口尺寸影响下次游戏视野。其他字段、注释和原始编码保持不变。

实现位于 `ezorsia/WindowScaling.cpp`。只管理 `MapleStoryClass` 主窗口，并只改写来自 `Gr2D_DX8.dll` 的窗口尺寸重设请求。引擎内部仍使用渲染尺寸；DirectInput 坐标保持原状，系统鼠标回退路径按当前客户区尺寸转换。

鼠标 API 调用点来自当前 BeiDou.exe 的 CInputSystem 反汇编（主模块相对返回地址 `0x19A3C5`、`0x19A8F1`、`0x19AC77`）。更换客户端 EXE 版本时需要重新验证这些调用点。

原有 `codecaves.h` 的 `fixMouseWheelHook`（`0x009E8090`）还负责从系统鼠标消息绘制手型光标。它现在先将消息中的窗口坐标换算成渲染坐标，再调用 `CInputSystem::SetCursorVectorPos`（`0x0059A0CB`），使手型与点击判定一致；保留滚轮跳过逻辑及窗口外坐标的正负号。DirectInput 分支不经过此转换。

## 构建和验证

使用已有的 MSVC x86 工具链和 Windows SDK，运行：

```powershell
.\tools\build-window-scaling.ps1 -ToolchainRoot <包含VC和Windows Kits的工具链目录>
```

产物为 `out/window-scaling/ijl15.dll`，脚本不会替换任何运行中的客户端文件。`-TestsOnly` 只构建和运行测试。

测试使用独立窗口和测试专用的 `Gr2D_DX8.dll`，覆盖等比尺寸计算、原生手型绘制桥接的坐标换算、八个拖动方向、缩小和放大、重复设备恢复尺寸请求、窗口样式恢复、最大化按钮与系统菜单、实际最大化和还原、最小化状态、系统窗口布局请求、其他窗口隔离，以及从 100% 渲染尺寸启动。保存测试使用隔离的 INI 文件，验证原始内容保留、普通和最大化状态的重启恢复、最小化/全屏退出，以及独立子进程直接 `ExitProcess`。模式切换测试复现 Gr2D 添加窗口样式、先发出仅更新边框的中间尺寸通知、再正式调整尺寸的顺序。显示验证只操作无激活测试窗口，不操作运行中的游戏。

这类测试验证窗口管理和坐标算法，不会启动游戏或触发真实 UAC。正式使用前仍需在游戏中检查拖动后的画面、菜单点击、滚轮、最小化恢复和真实 UAC 切换。
