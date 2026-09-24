# IP + password authentication (2026-09-24)

> 最新要求：HTTP＋密码，取消HTTPS/证书验证；默认密码 `kvm`，Windows为主要客户端。此条取代下文旧TLS/TOFU要求，见 [ADR-005](ADR-005-http-password.md)。

用户明确要求取消 pair/PIN 验证，使用 IP + 密码，初版默认密码为 `kvm`。本决策取代 AGENTS.md 第 6 条及 ADR-002 中保留配对的部分；输入加密、TLS、Linux 权限清理和设备授权约束继续保留。Sunshine/MPP 固定 commit 不变。

## Wire contract: password-v1

- HTTP `/serverinfo` 只用于发现，返回 `RKMoonAuth=password-v1`、`PairStatus=0` 及原有 HTTPS 端口。不得向 HTTP 发送密码。
- HTTPS `/serverinfo`、`/applist`、`/appasset`、`/launch`、`/resume`、`/cancel` 每次请求携带 `Authorization: Basic base64(UTF8("kvm:" + password))`。用户名固定为 `kvm`，UI 只需 IP 与密码。
- HTTPS 不要求客户端证书；已保存的配对证书不提供任何授权。每个请求独立验证，重复 Authorization header 拒绝。失败返回 GameStream XML `status_code=401` 并关闭连接，成功才进入原 handler。
- 认证后的 HTTPS serverinfo 返回 `PairStatus=1`，仅用于上游客户端状态兼容，不表示进行过配对。
- HTTP/HTTPS `/pair` 路由均移除（404），不启动或构建 Unix PIN 管理服务；旧 PIN 工具/源文件仅留作历史测试，不是当前入口。
- 密码仅在 TLS 内发送，客户端首次使用 TOFU 获取服务器证书，后续连接严格校验缓存证书；首次连接的 TOFU 不提供预先验证的主机身份。密码不写 URL、日志或客户端配置。当前初版服务端默认值固定，尚无修改密码的管理入口。

服务端比较 SHA-256 摘要时使用 OpenSSL 常量时间比较，限制 header 长度；全局失败预算为 10 次突发、每秒恢复 1 次，耗尽时正确密码也需等待预算恢复。这限制暴力尝试，但共享预算也可能使合法连接短暂被阻断。没有新增客户端状态表或持久化 token。

保留现有 GameStream TLS、加密控制/输入、RTSP、RTP/FEC、HID 独占和失败释放逻辑。原版 Moonlight 不支持此认证扩展；必须使用同步更新的定制客户端。

## Verification boundaries

验证分为生产密码校验器及路由 guard 的本地 C++ 合成请求测试、固定上游 overlay 回归、Linux 完整构建，以及授权窗口内的真实 HDMI/客户端测试。前两者不证明后两者。新结果见 `results/20260924-password/STATUS.md`。
