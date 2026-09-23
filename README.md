# RKMoon — RK3588 HDMI → 原版 Moonlight KVM

> 2026-09-23 本地更新：完整 Sunshine 与真实 MPP worker 已在 Linux amd64 编译链接通过；25 原生、69 Python/联合、25 sanitizer 测试通过。目标 ARM64/P1/P2/P3 仍未测试。最新结果见 [构建记录](results/20260923-build/STATUS.md)。下文原交接记录保留供追溯。

**版本：0.1.0-dev / 2026-09-23 云端源码交付。状态：可接手工程，不是已完成实机验收的安装发行版。**

独立 V4L2 / Rockchip MPP worker + 固定 Sunshine 的专用 HDMI 补丁 + 现有 kvmd USB HID 桥接。目标为原生 1080p60、HEVC Main 8-bit SDR，H.264 硬编码兼容路径；之后才开放原生 1440p60、4K60。

本包不覆盖 `nanopc-t6-kvm`，不实现 VNC 或定制 Moonlight，不通过桌面重抓屏绕路，不包含软件视频编码回退，不修改 EDID、USB gadget、内核或网络。所有新增组件源代码均在包内；Sunshine、MPP 等第三方完整源码通过固定 commit 下载。

## 先看状态，不要把编译通过当成设备可用

| 项目 | 本次结果 |
|---|---|
| V4L2/编码/输入真实源码入口分析 | 已完成，见 ADR 与 SOURCES |
| 核心库、V4L2 探测程序、原生测试构建 | x86_64 Linux 实际通过 |
| worker CLI 源文件独立编译 | 实际通过；未链接 MPP |
| 原生核心测试 | 25/25 通过 |
| Python / C++输入联合 / 补丁工具 / 部署工具测试 | 68/68 通过，无物理 USB |
| ASan/UBSan 原生核心测试 | 25/25 通过 |
| 完整 Sunshine checkout 上套补丁、链接 | **未测试**：构建容器 GitHub DNS 不通，仅连接器能读源码 |
| `encoder_mpp.cpp` 对真实 MPP SDK 编译链接 | **未测试**：容器无 SDK、无 RK3588 |
| T6 HDMI→HEVC、Moonlight 配对/硬解、USB 真机输入 | **未测试** |
| 延迟、画质、高分辨率、长稳 | **未测试** |

真实日志在 `results/cloud/`。测试用 fake backend / 协议夹具不进入生产 worker，不能代替上述未测试项目。

## 入口

本地 Codex **先读 [CODEX_START.md](CODEX_START.md)**，再读 [HANDOFF.md](HANDOFF.md)。优化计划在 [docs/PLAN.md](docs/PLAN.md)，架构门在 [docs/ADR-001-architecture.md](docs/ADR-001-architecture.md)。

```bash
# Linux；只有编译和离线测试，不会打开 HDMI 或写入 HID。
./tools/build.sh offline

# 只读设备基线；输出可能含设备名和进程 PID，分享前审阅。
python3 tools/probe.py --video /dev/video0 --output /tmp/rkmoon-baseline.json
```

不要先运行 `tools/run.py start`。必须先完成本地构建门、确认设备节点/输入布局、授权采集接管，并通过 P1 独立解码。实际单命令启动/停止及回滚在 [DEPLOY-ROLLBACK.md](docs/DEPLOY-ROLLBACK.md)。

## 两个会阻止“直接启动”的真实约束

**1080 高度不满足当前严格直接导入的 16 行对齐门槛。** 因此本版原生 1080p 需要显式 `--allow-copy`（或配置 `allow_cpu_pixel_copy: true`）。这里仅允许像素拷贝/转换，视频编码始终使用 MPP；日志会明确报告路径。不要把 1080p 零拷贝当成已实现验收的能力。

**旧记录里的 USB 鼠标是绝对模式。** 本包首版只转发相对鼠标，kvmd `/hid` 的 `mouse.absolute` 必须为 `false`。若为 `true`，先做视频闭环，输入阶段单独提出切换/增加相对 HID 的授权请求。不得静默改 descriptor、USB role、UDC 或 `/hid/set_params`。

## 边界

源码固定，但 OS/工具链/npm 等未做到逐字节可复现或全部 hash 锁定。完整 Sunshine 保留桌面相关构建依赖；运行路径绕开桌面捕获，不等于已证明可删除所有图形库。自动动态码率、绝对鼠标、HDMI 音频、HDR/10-bit/AV1、ATX、存储、手柄、公网部署不在本次已实现范围。

新增独立代码 GPL-3.0-or-later；专用 Sunshine 组合构建按上游 GPL-3.0-only 处理，详见 [LICENSES.md](docs/LICENSES.md)。
