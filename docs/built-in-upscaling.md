# 客户端内置 GPU 缩放

窗口放大时，低分辨率成品画面中的像素被直接拉大，容易出现笔画粗细不均和方块边缘。
本功能提供单次 GPU 双线性缩放，以及 CuNNy 神经网络重建后再适配窗口大小两种方式。
启动游戏后自动生效，无需打开 Lossless Scaling、Magpie 或其他程序。

## 使用与回退

备份并更新构建后的 **32 位 `ijl15.dll` 和 `BeiDouUpscale.dll`** 到游戏 EXE 同目录。在该目录的 `config.ini` 中添加：

```ini
[upscaling]
enabled=true
algorithm=cunny
quality=balanced
max_fps=60
```

- `quality=balanced` 使用 CuNNy-fast-NVL；`quality=fast` 使用较小的 CuNNy-veryfast-NVL。
- `algorithm=linear` 使用一次 GPU 双线性绘制，明确对齐像素中心；不加载神经网络着色器，
  不创建 2× 中间画面或 Lanczos 资源。只保留输入纹理和一个复制着色器。
  这会减少缩放开销，细小笔画通常比 CuNNy 柔和；需在实际地图和 UI 上验收。
- `max_fps=60` 优先使用窗口垂直同步。在 60Hz 显示设备上由 Present 控制节奏，
  不再叠加独立的 60 帧计时器。帧率上限低于设备刷新率时仍保留软件限帧；
  未知刷新率或垂直同步不可用时也保留计时器。接受 15–240 的整数；
  `0` 表示不限制，并切回立即提交。该项会在约一秒内重新读取，便于运行中对照。
- `enabled=false` 在下一次启动时直接转发系统原生 DX8。
- 缺省为关闭；未知算法、未知质量或无法解析的配置也会关闭此功能。
- 除 `max_fps` 外，设置在启动时读取；修改后需要退出并重新打开客户端。

退出客户端后运行部署脚本。它会备份原配置和两个 DLL，只更新 `[upscaling]`，保留其他配置字节：

```powershell
python -B tools/deploy-upscaling.py --client C:\Game\BeiDou-Client-research
```

已有最新 `ijl15.dll` 时，可只更新图形模块并切换到轻量模式：

```powershell
python -B tools/deploy-upscaling.py --client C:\Game\BeiDou-Client-research --algorithm linear --module-only --diagnostics
```

`--module-only` 保留现有 `ijl15.dll`；`BeiDouItemEff.dll` 不在本脚本的更新范围。
默认构建仍包含两种模式，改回 `algorithm=cunny` 并重启即可恢复神经网络效果。
`--diagnostics` 开启持续的低频帧时间记录，其他配置（包括 1080p 内部渲染与帧率上限）保留。

不要把仓库的配置模板直接覆盖到已配置的客户端。登录信息、Locale Emulator 快捷方式、
帮助菜单资源和内部渲染尺寸不需要更换。`ijl15.dll` 的这次更新只增加可选的模块加载入口，
构建基础包含既有的窗口修复和帮助菜单功能。

可在游戏目录的 `upscaling.log` 检查：

- `DX8 -> DX9` 表示已启用内置图形模块。
- `active: 1920x1080 -> ...` 表示最终呈现已走优化路径。
- `GPU upscale failed` 表示当前设备已回退原呈现，直到设备重置后才重试，避免每帧反复失败。
- `backend=single-pass-linear, CuNNy=no`（字段顺序可能不同）表示轻量窗口缩放。
- `interval=0x00000001, device_refresh=60Hz, max_fps=60, timer_fps=0` 表示输出
  交换链报告垂直同步，并停用重复的软件计时器；这是 API 参数记录，不能排除驱动强制
  覆盖同步设置，也不等于已经测量屏幕实际显示的帧间隔。
- `fullscreen: windowed=0, backbuffer=..., device_display=..., CuNNy=no` 表示 Direct3D
  真正全屏时保留原始呈现。`device_display` 是图形设备报告的模式，不能用它断言
  显示器物理面板分辨率、驱动/显示器的缩放算法或视觉效果。

窗口尺寸等于内部渲染尺寸时通过同一条同步输出路径精确复制，日志为 `backend=pixel-copy`。
禁用功能后不会写日志，原日志可能是上一次运行留下的。

## 图形路径与范围

`Gr2D_DX8.dll → ijl15 的可选加载入口 → BeiDouUpscale.dll → d3d8to9 → DX9 → 双线性 / CuNNy → 当前游戏窗口`

旧客户端会在启动时阻止目录中出现 `d3d8.dll`。因此本功能使用专用模块名，
只处理 `Gr2D_DX8.dll` 发出的 DX8 加载请求，不修改游戏 EXE 的启动检查。
其他模块加载系统 DX8 的行为不变。未启用或缺少专用 DLL 时保留原生加载路径。

CuNNy 模型移植为预编译的 DX9 Pixel Shader 3.0，与游戏共用图形设备。没有屏幕捕获、
CPU 逐帧回读、DX9/DX11 跨设备复制、外部模型下载或额外显示窗口。
原 DX8 渲染缓冲区大小保持不变，输出使用同一个 HWND 的额外 DX9 交换链。

常见非整数比例的 Lanczos2 权重在窗口改变时预计算，以 FP32 小纹理保存；采样倍率
不超过 1.5 时，六个采样点已完整覆盖原滤波器的有效范围。更大的缩小比例仍使用宽
滤波器。模型、抗振铃限制和显示尺寸保持一致，不通过降低画质来减少计算。

窗口帧率控制也覆盖 1:1 呈现；原生全屏保留游戏行为。输出交换链使用
[`D3DPRESENT_INTERVAL_ONE`](https://learn.microsoft.com/en-us/windows/win32/direct3d9/d3dpresent)，
避免立即提交与独立限帧产生刷新相位漂移。设备刷新率每秒检查一次；上限达到刷新率时
停用软件等待。较低上限、未知刷新率和同步创建失败时保留高精度可等待计时器，
不修改全局计时器精度，不在加载停顿后集中补交画面。较低上限和跨显示器情形的最终
显示节奏仍需实机验证。最小化、原生呈现、设备丢失及输出重建时清空窗口计时。

| 最终窗口大小 | 处理方式 |
| --- | --- |
| 与内部画面相同 | 精确复制，通过同一窗口同步交换链呈现 |
| `linear` 模式放大或缩小 | 一次 GPU 双线性绘制到窗口尺寸 |
| `cunny` 模式两个方向均放大 | 一次 CuNNy 2×，再按需用带抗振铃限制的 Lanczos2 适配窗口 |
| `cunny` 模式缩小 | 直接用 Lanczos2 缩小 |
| 原生独占全屏 | 保留原呈现，不执行神经网络缩放 |
| 不支持的呈现方式或 GPU 处理失败 | 保留原呈现 |

不重复运行神经网络放大超过 2× 的窗口。部分区域 Present 保留原裁切语义；此版本只
优化主设备的完整帧 Present，不处理游戏自行创建的额外 DX8 交换链。

已有窗口拖动、16:9 大小约束、最大化、鼠标映射、退出保存位置和 Alt+Enter 恢复规则
仍由 `ijl15.dll` 的窗口逻辑负责。内置模块在设备 Reset 和释放前清理自己的资源，
每帧恢复游戏的渲染状态、渲染目标和深度表面。

这是对成品画面的重建，不能增加原始字体/素材没有的细节。模型可能改变细小笔画的
形状，需要结合实际地图和 UI 选择质量。登录背景只覆盖 800×600 等资源布局造成的
黑边属于另一项适配工作，本功能会保留这些黑边。

需要 Windows 的 DX9 支持、Shader Model 3.0、FP16 渲染目标和足够的纹理尺寸。
DX8 转译还使用系统 `d3dx9_43.dll`；本机已存在。缺少该库或无法创建转译接口时直接
使用系统 DX8，不弹出安装器或打开网页。

## 构建

推荐先使用现有工具链脚本，生成结果在 `out/neural/BeiDouUpscale.dll`：

```powershell
.\tools\build-window-scaling.ps1 -ToolchainRoot C:\Game\BeiDou-Server\tools\msvc
.\tools\build-upscaling.ps1 -ToolchainRoot C:\Game\BeiDou-Server\tools\msvc -Python <python.exe>
```

需要 MSVC x86、Windows SDK、Python 3；GPU 参考比较额外需要 NumPy。
图像检查脚本需要 Pillow。构建包含预编译模型、真实 DX9 渲染测试、独立 CPU 模型
对照，以及真实 DX8 代理入口测试。无图形访问权限的构建环境可用 `-SkipGpuTests`，
之后必须在可使用 GPU 的环境单独执行测试。游戏运行时不需要 Python 或着色器编译器。

模型模块也可用 Visual Studio 的 Win32 CMake 构建；主客户端沿用上面的脚本：

```powershell
cmake -S . -B out/cmake-neural -A Win32 -DBUILD_UPSCALING=ON
cmake --build out/cmake-neural --config Release --target d3d8
```

依赖版本、来源和许可证见 [third_party/README.md](../third_party/README.md)。

## 验证边界

自动测试覆盖模型输出、1:1 精确复制、分数比例/缩小、游戏状态恢复、窗口反复调整、
逻辑缓冲区尺寸保持、托管纹理、游戏状态块、设备 Reset 和最终引用释放。
测试使用隐藏的自有窗口，不会关闭或操纵正在运行的游戏。

2026-09-13 本机验证：MSVC 14.44 / SDK 10.0.26100、x86 Release，脚本和 CMake 两种
构建均通过。GPU 为 NVIDIA GeForce RTX 5070 Ti Laptop GPU。独立 CPU 参考直接解释
上游 HLSL 的赋值与运算顺序，不复用生成器的模型解析结果；两档 GPU 输出的 RGB8
平均绝对误差分别为 0.265 / 0.430，99 分位均为 2，最大为 4。1:1 逐像素完全一致。

独立 GPU 基准（平衡模式，50 帧、前 10 帧预热后统计；不含游戏本身和显示同步）：

| 内部画面 → 最终窗口 | GPU 时间中位数 | GPU 时间 95 分位 |
| --- | --- | --- |
| 1280×720 → 1920×1080 | 0.702 ms | 0.829 ms |
| 1280×720 → 2560×1440 | 0.370 ms | 0.402 ms |
| 1920×1080 → 3840×2160 | 0.409 ms | 0.410 ms |

非整数比例包含额外重采样，因此不一定比整倍放大更快。实际开销会随 GPU、功耗状态、
窗口尺寸和游戏负载变化。中文宋体 12–20 像素测试图已检查平衡/快速/双线性/最近邻
对比；生成方法在 `tests/upscaling/visual.py`，不等同于实际游戏字体的验收。

2026-09-14 追加验证：专用模块加载测试已覆盖调用来源隔离、缺少模块时回退、关闭加载
入口；原窗口保存/鼠标映射回归和帮助菜单测试均通过。

research 实机通过中文区域启动器启动后，日志已确认
`active: 1920x1080 -> 3365x1893, CuNNy=yes`，质量为 `balanced`，没有 GPU 回退记录。
专用 DLL 的加载方式修复了初次采用目录内 `d3d8.dll` 时触发的旧客户端启动报错。

真实客户端仍需人工检查不同地图、中文聊天与提示、鼠标点击、最大化、
Alt+Enter、UAC 返回和退出重开。自动测试不能代替这些客户端兼容性检查。

## 帧时间优化与诊断

初次实机反馈清晰但卡顿后，内部统计发现此客户端在 60Hz 屏幕上提交约 300 fps。
常见分数比例的重复滤波计算已移到窗口变化时执行，并增加可配置的窗口帧率控制。
独立测试中，1920×1080 → 3365×1893 的平衡模式中位 GPU 时间从 1.48 ms 降至
0.78 ms；测试图相对原滤波器的最大通道误差为 1/255。GPU 型号和系统负载会影响
该数字，游戏的实际卡顿需以帧时间和操作体验评估。

2026-09-14 帧率控制版已部署到 research 客户端，经原中文区域启动器启动后确认
`1920x1080 -> 3612x2032, CuNNy=yes`，保持 `balanced` 模型。当前启动画面在加载
结束后的连续五个统计窗口中均为 60.0 fps，最大帧间隔为 17.30–17.53 ms；三次
进程 GPU 3D 引擎采样为 9.28%、10.21%、10.56%。启动阶段仍记录过 238.20 ms
间隔，因此这些数据不能代表加载、切图或所有游戏操作，也不能代替用户对卡顿的复测。

临时在 `[upscaling]` 加 `diagnostics=true` 并重启，可在 `upscaling.log` 持续查看窗口模式
的 FPS、最大帧间隔、CPU 提交和 Present 耗时，每五秒一条；不再限于前一分钟。
加载/切图会拉高最大间隔。
这些 CPU 计时不等同于 GPU 执行耗时；它们用于区分游戏循环、提交和等待。
默认不启用诊断，诊断不会采集画面、键鼠内容或账号信息。

轻量模式的测试数据、与全屏的区别及验收边界见 [轻量缩放验证](lightweight-window-scaling.md)。
