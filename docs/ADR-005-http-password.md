# HTTP + password only

2026-09-24 用户进一步明确取消 HTTPS 和证书验证，只保留基础密码认证，以 Windows 客户端为主要交付。本决策取代 ADR-004 的 TLS/TOFU 部分。固定 Sunshine/MPP commit 不变。

所有控制接口通过 HTTP，默认端口47989：`/serverinfo`、`/applist`、`/appasset`、`/launch`、`/resume`、`/cancel`。每个请求都必须带 `Authorization: Basic base64(UTF8("kvm:" + password))`；默认密码仍为 `kvm`，用户名由客户端内部固定。缺失、重复或错误凭据返回 GameStream XML `status_code=401` 并关闭连接。保留有界失败预算。

`serverinfo` 本身也要求密码，成功返回 `RKMoonAuth=password-http-v1`、`PairStatus=1`、`HttpsPort=0`。不再需要公开发现或任何证书绑定。没有HTTPS监听，不生成或读取服务端TLS证书；`/pair` 仍404，Unix PIN服务仍关闭。未知接口不提供授权功能。

客户端仅对用户选定的 HTTP host:port 添加凭据，禁用自动重定向，密码不放URL、不记日志、不写配置。按用户选择，HTTP控制流中的密码及会话建立参数没有TLS传输保护；GameStream原有输入/控制包加密、RTSP/RTP/FEC与HID独占释放实现仍保留，但不能将其称为端到端保密连接。

Windows客户端按sshh的VM200固定快照→独立9xxx克隆→QGA临时SSH租约→原生编译测试→Target SMB校验交付→关机/租约关闭/24小时销毁guard流程构建。不得以macOS测试代替Windows产物。

旧ADR-004与results/20260924-password记录属于先前HTTPS实现，不作为本版验收。新记录见 results/20260924-http-password/STATUS.md。
