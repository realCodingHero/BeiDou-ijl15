# 轻量窗口缩放

用户反馈真正全屏清晰流畅、GPU 占用较低，希望窗口也使用不依赖 CuNNy 的缩放。
本次将 `algorithm=linear` 优化为一张输入纹理、一个复制着色器和一次 GPU 双线性绘制，
跳过 CuNNy 着色器加载、2× 中间图像和 Lanczos 重采样资源。窗口仍复用原 HWND 和图形
设备，维持原有尺寸、鼠标映射、位置保存及 60 帧控制。

这属于普通 GPU 插值，并不能保证复现驱动或显示器在全屏时使用的同一个滤波器。
真正全屏继续使用游戏的原始 Present。新增日志会明确记录 `windowed=0`、回缓冲区、
设备显示模式和 `CuNNy=no`，避免仅凭视觉外观推断是否使用神经网络。

Direct3D [StretchRect](https://learn.microsoft.com/en-us/windows/win32/api/d3d9/nf-d3d9-idirect3ddevice9-stretchrect)
支持驱动滤波复制；本机放大测试发现其采样位置与标准像素中心双线性参考存在偏差。
最终实现采用明确半像素对齐的绘制；1:1 的 Render 测试使用不带滤波的精确复制，
实际游戏窗口 1:1 仍直接使用原始 Present。

## 自动验证

2026-09-14，MSVC x86 Release，RTX 5070 Ti Laptop GPU：

- 1:1 输出逐像素相同；1.5×、2×、非整数比例与缩小结果通过独立 NumPy 双线性参考。
  平均字节误差不超过 0.164，最大误差 2/255，排除了误用最近邻与半像素偏移。
- 轻量模式资源引用数在改变窗口目标尺寸时保持稳定，未创建神经网络资源。
  视口、混合和采样状态恢复、设备 Reset、最终 Release 均通过。
- CuNNy 原有独立参考、Lanczos、实际 DX8 代理入口的神经网络/轻量/禁用模式均通过。
- 配置更新保留 ANSI 字节、LF/CRLF、其他节、尺寸与帧率，支持反复切换算法。
- 中文宋体 12–20 像素、黑白边缘的 1.5×/2× 实际 GPU 输出已经目视检查。
  双线性减少不均匀方块，但细小笔画比 CuNNy 柔和；这不是游戏实机画质验收。

## 独立性能测试

每项 50 帧，前 10 帧预热；GPU 时间戳只测缩放工作，不包括游戏逻辑、显示等待或
实际窗口合成。客户端可能同时运行，功耗与其他负载会影响结果，不能据此宣称游戏
整体提速相同比例。基准程序为 `tests/upscaling/Benchmark.cpp`。

| 内部 → 输出 | 轻量 GPU 中位数 / P95 | CuNNy balanced GPU 中位数 / P95 |
| --- | --- | --- |
| 1920×1080 → 3221×1812 | 0.061 / 0.067 ms | 1.019 / 1.280 ms |
| 1920×1080 → 3612×2032 | 0.072 / 0.079 ms | 1.093 / 1.363 ms |
| 1920×1080 → 3840×2160 | 0.066 / 0.073 ms | 0.865 / 1.071 ms |

## research 验收

使用 `tools/deploy-upscaling.py --client <research目录> --algorithm linear --module-only --diagnostics`
部署候选版。正式客户端保留此前已经验收的 CuNNy 模块。

通过原中文快捷方式启动 research，日志应显示 `backend=single-pass-linear` 和 `CuNNy=no`。
对照相同地图的文字、人物、复杂技能场景，以及窗口与 Alt+Enter 真正全屏的效果。
帧时间每五秒持续记录；全屏绕过此窗口统计，不把两者混为同一测量。
清晰度和实际流畅度通过后再决定正式部署。若要回退，退出 research，恢复本次备份的
`BeiDouUpscale.dll` 与 `config.ini`；不覆盖当前正式客户端的配置。

2026-09-14 已部署 research 候选模块，SHA256 为
`86a1a514ce5f39baf8c886c492e42b2b5347a4a1e6483167f50375850520b518`，
备份位于 research 的 `upscaling-backups/20260914-135756-122877`。
原有 `ijl15.dll`、`BeiDouItemEff.dll` 和 `[upscaling]` 以外的配置字节均已核对未变。

13:59:36 启动的 research 日志确认 `1920x1080 -> 3612x2032`、`CuNNy=no`、
`backend=single-pass-linear`。启动阶段最大间隔 511.86 ms；随后连续八个五秒区间
记录 60.0 次呈现/秒，最大间隔 17.20–19.97 ms。此时正式客户端也仍在运行。
这些是窗口呈现调用统计，不能代替复杂场景的主观流畅度或最终显示帧时间验收。
后续 Alt+Enter 对照已产生全屏日志：`windowed=0, backbuffer=1920x1080,
device_display=1920x1080@60Hz, CuNNy=no, presentation=native`，窗口返回后恢复
`backend=single-pass-linear`。这确认游戏确实切换到原生 1080p 全屏；不能由此推断
GPU 扫描输出之后显示器使用的具体滤波器。用户对画质与实际流畅度的反馈仍待确认。
