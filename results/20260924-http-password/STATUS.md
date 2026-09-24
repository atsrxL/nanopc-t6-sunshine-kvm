# HTTP基础密码版本 — 2026-09-24

本次按用户要求移除HTTPS和证书验证。此前 `results/20260924-password` 是已被替代的HTTPS版本证据，不证明当前版本。协议见 [ADR-005](../../docs/ADR-005-http-password.md)。

| 检查 | 状态 | 范围 |
|---|---|---|
| 固定Sunshine overlay | 通过 | 精确验证前版树后，从固定git对象重生成；14文件，无未知修改覆盖 |
| ARM64 Linux完整编译链接 | 通过 | Debian trixie隔离容器，非T6原生 |
| 本地生产密码guard C++ | 通过 | 缺失/错误/重复/超长header、正确认证和限速，合成请求 |
| 原生离线测试 | 通过 | 28/28 |
| Python/联合/工具回归 | 通过 | 93项中91通过、2精确内核fixture缺失跳过 |
| 生产binary真实HTTP回归 | 通过 | 普通UID，六接口缺失密码401，错误密码401，正确serverinfo/applist，pair404 |
| 无TLS依赖的运行路径 | 通过 | 仅监听47989/48010，无47984，无PEM生成；没有证书请求/绑定 |
| 重启和权限/资产拒绝 | 通过 | 重启身份保持；root/相对配置/缺资产拒绝；正常关闭41ms exit0 |
| Windows客户端构建/交付 | 未测试 | 子代理按sshh流程推进，最终证据待补 |
| T6部署及真实HDMI/声音/USB | 未测试 | 未授权访问设备，无媒体流验收 |

二进制及服务端源码SHA256见 `SHA256SUMS`。overlay补丁SHA256 `b00946aed86fd1a9a8f70a09e2f081309ac3c32109c76466f64ff568e6f6fe60`。完整私有日志位于 `private/results/local/password-http/`（build.log、offline.log、startup-check.log、startup/synthetic-host-startup.log）。构建和启动使用已存在的任务隔离镜像，不映射HDMI、MPP、音频或USB设备。

保留GameStream的输入/控制包加密实现；HTTP控制通道没有TLS保密性，不能据此声称输入内容端到端保密。
