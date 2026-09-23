# RKMoon — NanoPC-T6 HDMI → Moonlight KVM

独立 V4L2 / Rockchip MPP worker、固定 Sunshine 网络栈抽取的无 Web 管理服务、kvmd USB 输入桥接，新增显式 HDMI ALSA→Opus 音频。允许定制 Moonlight 客户端；生产视频始终为 MPP 硬件编码。最小服务分支构建/实机状态与旧 `main` 基线分开记录。

## 当前验证状态

2026-09-23 已完成 Linux amd64 和 ARM64 全量构建，T6 原生 1080p60 HEVC Main8 / H.264 Baseline 各 60 秒采集及独立解码。原版 macOS Moonlight 6.1.0 已配对并显示真实 HDMI，HEVC 使用 VideoToolbox 硬解与 Metal 渲染。持续连接和重连的最终结果见 [T6 验收记录](results/20260923-t6/STATUS.md)。

以上视频数据属于旧 `main` 基线；其 P3 相对 USB 已完成混合事件观察但异常释放等场景未全验收。`feat/minimal-kvm-server` 新 binary、真实 HDMI 声音与定制客户端均需另验，不可借用旧结果称已完成。

- 原生测试：25 项；Python / C++ 联合与工具测试：69 项。
- amd64 ASan/UBSan：25 项。ARM64 模拟构建测试与真实硬件验收分别记录。
- 1080p BGR24 使用显式行拷贝至对齐缓冲，再由 MPP 硬件转换颜色和编码，不是零拷贝。
- HEVC/H264 dequeue→AU 中位约 9.5 ms，不包含 HDMI 前端、网络或客户端显示。

## 构建与运行

详见 [隔离构建](docs/LOCAL-BUILD.md)、[构建依赖](docs/BUILD.md) 和 [独立部署与回滚](docs/DEPLOY-ROLLBACK.md)。

```bash
# Linux；不打开 HDMI、不写 HID
./tools/build.sh offline
python3 tools/fetch_sources.py
python3 tools/apply_sunshine.py vendor/sunshine --patch-output /tmp/review.patch
# 审阅后在独立 checkout 应用
python3 tools/apply_sunshine.py vendor/sunshine --apply
./tools/build.sh mpp
./tools/build.sh sunshine -DGLAD_SKIP_PIP_INSTALL=ON
```

构建输出为独立 `build/sunshine/rkmoon-kvm`（不生成 Web UI，不需 npm）。启动配置默认不授权采集、输入/声音关闭。要采集被控机声音，先只读确认 T6 ALSA 的真实 HDMI 录音设备，再配置 `audio.enabled=true` 与显式 `alsa_device=hw:...`（必要时经审核使用可能转换采样率的 `plughw:`）。管理 PIN 不走网页：`python3 tools/admin.py --state /private/rkmoon list`，再 `python3 tools/admin.py --state /private/rkmoon pin --id <32hex>`，仅在终端交互输入 PIN。运行需普通用户的特定视频/MPP/DMA/ALSA 设备权限及独占采集窗口，不应与旧 KVM 抢占节点；需保留私有 assets 与运行库。

## 范围与证据

代码保留 Sunshine 配对、认证、加密和权限清理，不改写 GameStream 网络协议。没有软件编码回退，不自动修改 EDID、内核、USB gadget、网络或旧服务。

最小服务分支尚未验收：新 binary 实机与客户端视频/USB、真实 HDMI 音频和音画同步、物理端到端延迟、拥塞恢复、1440p/4K、长稳、HDR、AV1、手柄。自动动态码率只具备 worker 控制原语。具体问题见 [OPEN-ISSUES](docs/OPEN-ISSUES.md)。

[首次构建记录](results/20260923-build/STATUS.md)、[T6 实机记录](results/20260923-t6/STATUS.md)、[架构](docs/ADR-001-architecture.md)。results/cloud 是交接包历史证据。视频、凭据和配对密钥不提交。

新增代码 GPL-3.0-or-later；专用 Sunshine 组合构建遵循上游 GPL-3.0-only，详见 [许可证说明](docs/LICENSES.md)。
