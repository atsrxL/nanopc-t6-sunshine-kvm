# 1440p90 server 源码实验构建记录

2026-09-24。MS-A2 在线但保存 SSH 认证失败，未再尝试；按 sshh 的非 remote-only 回退规则，使用已安装的本地 ARM64 Docker 镜像 rkmoon-password-build:local 构建，无新工具链安装。构建为 Linux ARM64/QEMU 容器，不是 T6 原生。

固定 MPP 0986d01294d5c2449c14cf13af9b740368c33967，Sunshine 63d35f702ee9e362e43263742981836ec0710384。产物及 SHA256：

- build/1440p90/native/rkmoon-worker — 9158a60d319e28b818e900c5d7bfc7336a2109cddb607091023944f824269a3e
- build/1440p90/prefix/mpp/lib/librockchip_mpp.so.0 — 0239793187d933bc501692c347113b200bc5e3383afcd78d67942ddaf19d2e22；SONAME librockchip_mpp.so.1
- build/1440p90/sunshine/rkmoon-kvm — de8617a7626aa95978a49e2fdbd8460471a837202a94c688f930a391175ec111

worker 依赖 MPP、libstdc++、libgcc_s、libc；构建 RUNPATH 为容器内路径，移至 T6 时需匹配 SONAME 的现有 MPP 库或显式运行库路径。T6 已由父代理在独立目录验证 worker SHA/ldd，尚未执行 probe/编码；后续 SSH 认证拒绝，停止重试。原 EDID/1080p60 已恢复。

本地结果：真实 MPP worker 与专用 Sunshine 均完整链接；30 项原生 core 测试通过，93 项 Python/联合测试中 91 通过、2 原条件跳过；运行配置 gate 定向 15 项通过。真实 1440p90 raw MMAP 30 秒结果由父代理记录在 CAPTURE.md，仅证明采集。真实编码、独立解码、Sunshine 会话与 Windows 客户端均未测试。保留 build/1440p90 与本地 Docker 镜像供父代理后续解码及复测；未部署 server，未清理该构建环境。
