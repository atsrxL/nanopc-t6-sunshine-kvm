# 接手代理约束

> Windows交付要求：SMB Target中交付解压后可直接运行的客户端文件夹，使用稳定目录 `RKMoon-Windows-x64`。每次先完成新版本复制及目标读回校验，再移除旧RKMoon客户端版本/ZIP及其sidecar；不要删除其他项目或驱动。用户不接受仅ZIP交付。

> 最新要求：HTTP＋密码，取消HTTPS/证书验证；默认密码 `kvm`，Windows为主要客户端。此条取代下文旧TLS/TOFU要求，见 [ADR-005](docs/ADR-005-http-password.md)。

任务是完成独立 RK3588 HDMI→Moonlight 工程，不是改旧 VNC。先读 CODEX_START.md、HANDOFF.md、docs/ACCEPTANCE.md。

1. 新目录工作；不得覆盖 `/Users/at/Documents/GitRepository/nanopc-t6-kvm` 或在旧仓库直接应用补丁。
2. 源码下载/本地构建/离线测试可直接做；读取现有设备前确认授权访问范围。不得从旧文档搬运凭据进仓库或日志。
3. 没有明确授权，不停止/重启旧 KVM，不打开争用中的 HDMI 流，不写 USB HID，不改内核、引导、CMA、EDID、USB role/UDC、网络、防火墙、软件源。
4. 固定 Sunshine / MPP commit。锚点失败先读实际代码，不把整段替换放宽为模糊吞掉未知版本；升级必须写 ADR 及重跑安全回归。
5. 不加入 CPU 视频编码或桌面重抓屏“临时兜底”作为正式路线。允许显式像素拷贝，不得假报 DMA-BUF。
6. 按用户 2026-09-24 指示取消 Sunshine PIN/pair，改用 IP＋密码（见 docs/ADR-004-password-auth.md）；保留输入加密、密码认证及 Linux 权限清理；不要 root/setcap 启动专用实例。
7. 不记录按键内容、密码、配对密钥；只提交脱敏的统计/校验值。硬件检查和故障日志先留 private/results/local。
8. 模拟/桩、语法编译、x86 构建、MPP 黑帧能力探测、真实 HDMI 测试、Moonlight 客户端验收必须分开标记。
9. 任何失败：保留证据、释放本实例资源，停止阶段推进；不能把未测试改成通过。HID release 失败要阻止下一租约。
10. 按 P0-build → P1 → P2-video-only → P3-input → P4/P5 顺序推进。每个阶段更新验收表和接手文档，禁止批量重写协议。
