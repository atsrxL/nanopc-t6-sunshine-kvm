# RKMoon — NanoPC-T6 HDMI → Moonlight KVM

独立 V4L2 / Rockchip MPP worker、固定 Sunshine 专用补丁、kvmd USB 输入桥接。使用原版 Moonlight，生产视频始终为硬件编码。

## 当前验证状态

2026-09-23 已完成 Linux amd64 和 ARM64 全量构建，T6 原生 1080p60 HEVC Main8 / H.264 Baseline 各 60 秒采集及独立解码。原版 macOS Moonlight 6.1.0 已配对并显示真实 HDMI，HEVC 使用 VideoToolbox 硬解与 Metal 渲染。持续连接和重连的最终结果见 [T6 验收记录](results/20260923-t6/STATUS.md)。

USB 输入仍关闭，尚未进行实际 USB 输入验收；当前不是完整 KVM 实机验收完成的发行版。

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

启动配置默认不授权采集、输入关闭。运行需普通用户的特定视频/MPP/DMA 设备权限及独占采集窗口；不应与旧 KVM 同时抢占节点。Sunshine 必须保留构建时指定的 assets 路径及匹配的私有依赖库。

## 范围与证据

代码保留 Sunshine 配对、认证、加密和权限清理，不改写 GameStream 网络协议。没有软件编码回退，不自动修改 EDID、内核、USB gadget、网络或旧服务。

当前未验收：USB 相对输入、物理端到端延迟、拥塞恢复、1440p/4K、长时间稳定性、音频、HDR、AV1、手柄。自动动态码率只具备 worker 控制原语。具体问题见 [OPEN-ISSUES](docs/OPEN-ISSUES.md)。

[首次构建记录](results/20260923-build/STATUS.md)、[T6 实机记录](results/20260923-t6/STATUS.md)、[架构](docs/ADR-001-architecture.md)。results/cloud 是交接包历史证据。视频、凭据和配对密钥不提交。

新增代码 GPL-3.0-or-later；专用 Sunshine 组合构建遵循上游 GPL-3.0-only，详见 [许可证说明](docs/LICENSES.md)。
