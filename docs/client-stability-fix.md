# 分辨率补丁与 ItemEff 崩溃修复

## 根因与改动

2026-09-14 的小电视崩溃发生在 `BeiDou.exe+0xD59B2`：原指令
`81 F9 58 02 00 00` 被错误写成 `81 38 04 00 00 00`，将寄存器比较变为
读取 `[eax]`。转储中 `eax=0x93`，因此发生读取访问冲突。

针对当前 EXE（SHA256
`1198fa57ca5a7c489bae43ec13c69681d9cabe0f96762f3dc0357facf2e7d4df`）修复：

- `0x004D59B2`：高度常量偏移由 `+1` 改为 `+2`，保留 opcode/ModRM。
- `0x004CC160`：宽度常量偏移由 `+1` 改为 `+3`，保留 EBP 寻址。
- 删除 `0x0064061D+1` 的写入；该处为 `idiv`，高度已在 `0x00640618+1` 设置。
- 删除 `0x00A5FC2B` 的写入；它把异常处理入口的 `mov eax,...` 改成了
  `add eax,...`，与分辨率无关。保留原 EXE 指令。
- 删除重复写入及两个无效果的 opcode 重写；显式区分互斥的登录布局补丁。

分辨率修改使用 `ResolutionPatch::Batch` 收集，启动时在安装运行时 hooks 前执行。
302 个允许的修改位置均有原始指令/数据字节、精确写入位置、长度和类型校验。
`tools/audit-resolution-patches.py` 使用 Capstone 检查完整立即数/位移字段、跳转
补丁边界、受支持的 EXE 标识及互斥分支。修改到 opcode、ModRM、另一条指令、
未登记位置或重叠应用将被拒绝。

运行时先检查全部签名和内存页，再开放写权限、写入、回读并刷新指令缓存。
写入中途失败会还原已修改的字节和页保护；只有完整的原始状态或本模块上一次
成功应用的完整状态可作为基线。未知版本、外部修改或失败结果会写入
`patch-integrity.log` 并停止启动，不继续运行部分修改的客户端。
旧代码洞含绝对返回地址，因此明确拒绝不同的加载基址，而不是声称支持 ASLR。

另一个进程的 `BeiDouItemEff.dll+0x1427` 崩溃发生在全局特效缓存的 CRT 析构路径。
全局缓存由持有 WZ COM 指针改为仅保存道具 ID；需要时在有效的游戏回调中获取并
释放属性对象，避免 DLL 卸载时访问已经销毁的资源。详见 `itemeff/README.md`。

CuNNy 模型、滤波器、60 帧控制、窗口/鼠标处理和客户端数据资源未改动。

## 构建与验证

```powershell
python -B tools/audit-resolution-patches.py --exe C:\Game\BeiDou-Client\BeiDou.exe --capstone-path <existing-python-packages>
.\tools\build-patch-integrity.ps1 -ClientExe C:\Game\BeiDou-Client\BeiDou.exe
.\tools\build-window-scaling.ps1 -ToolchainRoot C:\Game\BeiDou-Server\tools\msvc
.\tools\build-itemeff.ps1 -KaentakeRoot C:\Game\BeiDou-Server\tools\kaentake-src
```

仅在人工审计新增补丁后使用 `--write` 更新清单；不要把未知客户端字节自动登记
成受信任基线。普通构建和游戏运行均不需要 Capstone。

自动验证已通过：

- 在真实 EXE 的独立内存副本上执行生产 `UpdateResolution()`，覆盖 64 组尺寸与
  布局设置、重复应用、重配置、签名不匹配、重叠写入和中途失败后的完整回滚。
- 实际执行修复后的比较与栈局部变量写入指令，使用崩溃时的 `eax=0x93`，并检查
  高度边界前后结果；确认除法和异常处理入口保持原始字节。
- 既有窗口/鼠标、最大化、位置保存、ExitProcess 和帮助菜单测试。
  隐藏窗口测试允许较大跟踪尺寸，避免当前 1440×2560 竖屏限制测试窗口宽度；
  最大化测试仍使用真实显示器工作区，生产窗口代码未改变。
- 实际 ItemEff 缓存的 407 个条目、20 次资源销毁后 DLL 卸载测试。

## 部署与验收

退出目标客户端后运行：

```powershell
python -B tools/deploy-client-stability.py --client C:\Game\BeiDou-Client-research
```

脚本验证 EXE 哈希，备份并更新 `ijl15.dll`、`BeiDouItemEff.dll`，保留配置和现有
`BeiDouUpscale.dll`。备份位于目标目录的 `stability-backups`，包含原 DLL、配置
和部署清单。回退时退出游戏，恢复该次备份中的两个 DLL 即可。

2026-09-14 用户确认 research 客户端测试通过，并授权同步正式版。research 的
`patch-integrity.log` 在 12:57:13 记录全部签名通过及补丁应用成功。

正式目录 `C:\Game\BeiDou-Client` 已通过同一脚本更新，使用 research 实机验收的
原二进制文件，未重新编译。两个 DLL 的 SHA256 均与 research 一致：

- `ijl15.dll`：`d030dfe1b1be168650a1f86afc9bcab6e5b1a7b20d4c9f6b7f42a3781431ddd7`
- `BeiDouItemEff.dll`：`f676ed85390df9351feb23792eed87ad8a9631e44c18b4525fb0740f6219a4a3`

正式版 `config.ini` 和 `BeiDouUpscale.dll` 的部署前后哈希保持一致。
本次正式版备份目录为 `stability-backups/20260914-125937-385088`。
实机验收来自 research；本次正式版同步完成文件与备份核对，未另行启动正式游戏。
