# 2026-09-23 本地接手验证

环境：MS-A2 VM 301 内独立 Debian 13 amd64 构建容器，GCC 14.2、CMake 3.31.6、Python 3.13。这是 x86_64 编译验证，不是 RK3588 硬件验收。

| 检查 | 结果 |
|---|---|
| 固定 Sunshine 完整 checkout、七文件补丁 | 通过；生成器输出与实际构建树一致 |
| 核心/V4L2 编译与 CTest | 25/25 通过 |
| Python、原生 HID 联合与工具测试 | 69/69 通过；模拟 HID 后端 |
| ASan/UBSan 原生测试 | 25/25 通过 |
| 固定 MPP SDK 与 worker 编译链接 | 通过，使用私有 librockchip_mpp |
| 完整 Sunshine 与 Web assets 编译链接 | 通过 |
| T6 只读设备基线 | 已取得，原始记录留在 private/results/local |
| ARM64 目标构建、MPP 硬件探测、真实 HDMI 双 codec | 未测试 |
| 原版 Moonlight 配对、硬解、实际 USB 输入 | 未测试 |

修复：平台源文件使用明确的 `src/rkmoon/rkmoon_bridge.hpp` 路径，避免添加整个 src 搜索目录后与 FFmpeg 的同名 C 头文件冲突；明确 build assets 路径；自定义构建根目录传入原生 HID fixture 路径；基线工具识别实际 t6-kvmd 服务；清理新增源码编译告警。新增 CI 和隔离构建 Dockerfile。

实机：FriendlyElec NanoPC-T6 LTS，Armbian 26.5.1 trixie，内核 6.1.115-vendor-rk35xx。旧 t6-kvmd、t6-kvmd-vnc、t6-kvm-panel 服务均运行。普通用户不能访问 MPP/dma_heap，尚未改权限、停服务、采集或写 HID。v4l2-ctl 缺失，实际 timings/format 仍待目标原生 probe。

完整构建日志和二进制清单保存在 VM 301 的 `/root/rkmoon-artifacts/20260923/`；`linux-amd64-build.tgz` 仅为编译产物证据，不是 ARM64 安装包。Sunshine 的绝对资源路径绑定构建目录，迁移需重建。本目录提交脱敏日志和 SHA256SUMS。历史 results/cloud 保留，不能与本次测试合并计算。
