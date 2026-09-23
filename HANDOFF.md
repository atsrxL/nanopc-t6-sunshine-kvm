# RKMoon 接手说明 — 2026-09-23

> 实机更新：ARM64 worker/Sunshine 已构建；T6 真实 1080p60 HEVC/H264 各 60 秒及独立解码通过；原版 Moonlight HEVC 硬解超过 11 分钟、H264 重连通过。USB 尚未完成。见 [T6 验收记录](results/20260923-t6/STATUS.md)。

> 2026-09-23 本地更新：完整 Sunshine 与真实 MPP worker 已在 Linux amd64 编译链接通过；25 原生、69 Python/联合、25 sanitizer 测试通过。目标 ARM64/P1/P2/P3 仍未测试。最新结果见 [构建记录](results/20260923-build/STATUS.md)。下文原交接记录保留供追溯。

## 用户目标和已做决策

独立 NanoPC-T6 / RK3588 HDMI KVM，原版 Moonlight、HEVC Main8 硬编码优先、H264 硬编码回退、USB 输入，而非 T6 本机 uinput。用户不要求复用旧 VNC 架构。保留旧方案作为回滚，不擅自停旧服务。

当前决策 B：独立 V4L2/MPP worker + 专用 Sunshine 薄适配层 + 独立输入租约服务调用既有 kvmd。控制 IPC 自定义但仅为同机私有协议；**GameStream 网络协议没有重写**。

## 固定源码

| 名称 | Commit | 用途 |
|---|---|---|
| Sunshine v2026.914.233613 | `63d35f702ee9e362e43263742981836ec0710384` | 配对/会话/加密/RTP-FEC 与平台输入入口 |
| Rockchip MPP | `0986d01294d5c2449c14cf13af9b740368c33967` | 编码 SDK/runtime |
| nanopc-t6-kvm | `6c381b30f8fc3b34a70a63840eb817f549e844b2` | 底层设计与旧实测参考，不作为依赖安装 |
| kvmd v4.120 | `78ff181e95b14327831441d58f2f7f4cb2181cde` | 本包核对的 HID API；实机版本必须另记 |

机器可读清单是 `sources.lock.json`。Sunshine 上游 C++23；本项目独立 worker C++17。不要误以为同一编译器最低版本完全相同。

## 代码导览

`src/capture.cpp`：MPLANE 的单 plane 原生 NV12/BGR24、timings/layout 校验、4 个 MMAP buffer、可选 EXPBUF、仅丢未编码 raw、源变化终止。不写 S_FMT/EDID。

`src/encoder_mpp.cpp`：真实 MPP API、Main8 HEVC/Baseline H264、明确导入或拷贝路径、CBR/GOP/强制 IDR、完整 AU 和 PTS 校验。**尚未对真实 SDK 编译链接**。最先完成这一门。

`src/core.cpp` / `annexb.cpp`：64 字节版本化 IPC 头、有限 AU、绝对 I/O deadline、序列校验、首帧/恢复 IDR 与 inline 参数集检查；不是完整 H264/HEVC 语法解析器。

`src/worker_main.cpp`：独立码流测试、CSV 与统计、MPP 合成黑帧能力探测、受控子进程。普通采集无合成画面和软件编码兜底。

`sunshine/` + `tools/apply_sunshine.py`：固定 commit 的补丁生成器，修改 7 个上游文件，再增加自有辅助源码。**不含对完整上游树实际套用/编译通过的声明**。默认 dry-run，锚点缺失/重复及脏树拒绝。小型锚点测试已通过，不等同完整套补丁通过。

`src/hid_client.cpp` + `python/rkmoon_hid/`：SO_PEERCRED、私有 UDS、单租约、有界键鼠队列、心跳、失败阻断与 release retry。kvmd 接受事件不等于 USB host 已消费。

`tools/run.py`：专用二进制 marker 检查、独立 HOME/XDG/state、前台监督、pidfd 安全停止、子进程 parent-death SIGTERM、停止后单独 release-all。`systemd/` 只是模板，未安装未 enable。

## 本次证据

云端 x86_64 Linux，无 RK3588/MPP SDK、无 Moonlight 客户端。25 原生测试、68 Python/联合/工具测试通过；ASan/UBSan 25 原生用例通过；worker CLI 独立编译通过。见 `results/cloud/`，记录有实际命令/日志。编译容器无法解析 GitHub，GitHub 连接器可读公开源码，故完成了源码分析而没有完整源码 checkout 构建。

## 最高优先级风险

1. **全量构建门未过。** 核对实际 MPP API/配置项、包 PTS、EOI、import 缓存/同步和错误清理；固定 Sunshine 的所有锚点/类型/目标链接。
2. **1080p 对齐。** 直接路径要求 width%64=0、height%16=0、精确 stride/sizeimage。本版1080p需要显式 `--allow-copy`。不能悄悄放宽布局、借用超出 buffer 的高度或改源模式。
3. **输入布局。** 旧记录 mouse.absolute=true；本实现拒绝。不能仅把相对 delta 写给绝对 HID。先视频，后在授权窗口解决 descriptor/kvmd 相对实例。
4. **客户端/无音频。** 原版配对、HEVC 硬解、H264 fallback、无音频可连接、重复连接、恢复控制都需要真实客户端验证。
5. **低延迟不是承诺。** 发送入口 budget 200ms 是停止积压阈值，不是目标延迟；不包含 HDMI dequeue 之前和客户端显示。kvmd HTTP 逐事件转发的延迟/队列必须实测。
6. **部分功能仅控制原语。** worker 具备降码率指令，Sunshine 自动动态码率链路未接；不能宣称自适应码率完成。
7. **输入最后一公里。** 断开后的逻辑 release/retry 与 SIGKILL 联合测试通过，不代表 USB 物理断开或 kvmd 卡死时能够绝对保证目标已释放；异常必须锁住接管权并报告。

## 下一步顺序

执行 CODEX_START.md；不再扩展功能优先级。先全量构建、P1 的真实 1080p60 HEVC/H264，再 P2 video-only，最后 P3。每项未测试保持未测试，不拿旧日志替代。
