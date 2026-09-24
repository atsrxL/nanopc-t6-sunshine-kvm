# IP＋密码认证验证 — 2026-09-24

本次不访问设备、不启动 HDMI/USB 会话、不修改旧服务。Sunshine 固定 `63d35f702ee9e362e43263742981836ec0710384`，MPP pin 未变。需求及协议见 [ADR-004](../../docs/ADR-004-password-auth.md)。

## 已完成的本地验证

| 检查 | 状态 | 范围 |
|---|---|---|
| 固定 Sunshine 全量 overlay | 通过 | 14 文件精确锚点，干净独立 checkout dry-run/apply；补丁 SHA256 `1789c1053940a8624f0023bce9375099847859b6b972701d4bf9c25174851380` |
| 生产密码校验器 + 生成的路由 guard | 通过 | macOS ARM64 与 Linux ARM64 C++；六端点正确/缺失密码、重复 header、错误密码、超长 header、失败限速；合成请求，不是网络握手 |
| Linux 离线原生测试 | 通过 | 28/28，GNU C++14.2、Debian trixie ARM64 容器 |
| Linux Python/联合/工具测试 | 通过 | 93 项：91 通过，2 项精确内核 fixture 缺失而跳过 |
| overlay 回归及日志脱敏 | 通过 | 14 项 overlay、5 项日志脱敏；真实固定源码与合成 sink |
| 新完整服务端构建 | 通过 | ARM64 Linux完整链接，非 T6 原生；build/password/sunshine/rkmoon-kvm |
| 新二进制模拟 worker + 真实 HTTPS | 通过 | 非root、六端点缺失密码及错误密码拒绝、正确密码serverinfo/applist、pair404、无PIN socket、重启身份稳定；未启动媒体流 |
| 客户端本地构建及认证测试 | 通过 | macOS ARM64完整链接、3组源码回归、10/10 Qt测试含合成loopback TLS；非生产服务端联调 |
| 新客户端与真实 HDMI/声音/USB | 未测试 | 需要独立硬件授权窗口 |

Linux 容器由 `tools/Dockerfile.build` 构建，镜像 `rkmoon-password-build:local`，image ID `sha256:82594bcf054f2bf7805c069893c8906658b3cd76d90f2775b063c71142511128`。不是 T6 原生运行；没有映射采集、MPP 或 HID 设备。

私有完整日志保存在 `private/results/local/password-auth/`：`fetch.log`、`docker-build.log`、`offline.log`、`sunshine-build.log`、`password-linux.log`、`overlay-final.log`、`redaction-final.log`。这些日志不是发布产物。自有服务端修改的源码摘要见 `SHA256SUMS`。

客户端实现与测试由客户端子代理在共享目录完成，最终状态见 `client/docs/ACCEPTANCE.md`；不能以源代码测试代替真实会话。

服务端二进制 SHA256：04600ec8d941f825d42d8d7d99bc89c6c745bee2254709cc5472b2eebc37c78f。启动回归私有日志 startup-check.log；关闭耗时40ms、exit0。root启动拒绝检查通过。临时合成证书/状态已清理。

生产binary的debug启动日志已检查：未出现Authorization、合成Basic凭据、密码内容或PEM内容；只保留脱敏请求标记。完整动态库路径列表留在私有 binary-libraries.txt。
