# HDMI 模式自动跟随与本地鼠标指针

用户要求：恢复 Ctrl+Alt+Shift+C 本地指针显示；客户端连接后自动读取实际 HDMI 尺寸/刷新率，并随源变化重连；视频设置仅保留码率与编码。协议见 [ADR-007](../../docs/ADR-007-source-display-discovery.md)。

## 当前验证阶段

- 服务端新增认证后的 `/serverinfo` display version 1 字段，250ms 缓存，只调用 `VIDIOC_QUERY_DV_TIMINGS`；不调用会改变此驱动 DMA 布局的 G_FMT。
- 客户端每秒异步检查；允许 0.15Hz 抖动；源变化先结束会话并等待清理，再按新模式发起。无信号等待，用户退出禁止自动重启。
- 本地指针显示保持相对 USB 鼠标协议。
- 部署前 T6 只读复查：`rkmoon-http-live` active，真实时序 2560×1440；现有输入租约标记存在。

## 服务端构建与 T6 部署通过

- ARM64 Sunshine 完整链接；31/31 原生测试，Python/联合 92 通过、2 条件跳过。生产程序合成 HTTP 认证、无设备字段、无 pair/HTTPS、重启身份检查通过。见 [构建记录](../20260924-display/SERVER-BUILD.md)。
- 二进制 SHA256：`47cd6a1f5e0732d9a1cc7c61df757ba6da11285e0e0cb7acab2afadbdebdf150`，126,974,152 bytes。
- T6 独立路径 `/home/at/rkmoon-display-follow/rkmoon-kvm`；沿用既有 worker、状态目录与主机 UUID。旧二进制保留；配置/unit 备份于 `/root/agent.backup/rkmoon-display-follow-20260924/`。
- 停止旧实例后，确认 `hid-recovery-needed.json` 已释放，才切换 runtime 并以 at 用户启动。`rkmoon-http-live` 与 `rkmoon-input` 均 active。
- 从 LAN 验证缺失/错误密码返回协议401；正确密码返回 version1、ready、2560×1440、fps_x100=8999。T6 只读时序再次确认2560×1440。
- 连续10次、每秒一次认证轮询均为相同 ready 模式；单次9.62–59.07ms。错误/缺失密码应答均不含画面信息字段。原始脱敏记录 `private/results/local/display-follow/t6-poll.json`。
- 服务仍是 transient unit，未新增开机自启或持久 EDID。

## Windows 新包已交付

- `smb://192.168.123.10/zssd/Target/RKMoon-Windows-x64-auto-display-1b610d1bf94a.zip`
- 29,321,156 bytes；SHA256 `475e71cd6ce075768fb819b078c6a7297c05cce5ed01528ef90ddb90d1705c07`。构建代理与父代理均从 Target 完整读回核对，同目录 sidecar 匹配。
- Windows VS2022 x64 Release、Qt17/17、gain、clean-PATH4秒启动通过；Mac Qt17/17、Python3/3通过。
- 点击连接后自动打开；设置仅码率/编码。源变化、安全清理、用户退出与无信号恢复的测试为合成 loopback/状态机验证。
- 本地指针显示时仍为相对 HID，受本地指针边界限制；再次隐藏恢复 raw relative 捕获。
- VM9006与VM200已停机，临时SSH关闭、控制端私钥删除，cleanup guard CHECK_OK，同项目保留期限2026-09-25 11:11:02 CST。

真实 Windows 新版画面、快捷键指针呈现与现场 HDMI 模式切换仍未验收。现有服务端真实时序发布通过不替代这些端到端测试；真实 HDMI 音频仍未通过。共享仓库未提交。

## 首帧退出缺陷（用户实测发现，修复中）

auto-display-1b610d1bf94a 包实际出现：首帧可见后立即回到GUI，无错误。T6日志11:20–11:21三次连接后约0.55–0.76秒断开，服务与HDMI时序正常。

根因：新控制代码首次 `SDL_RegisterEvents(1)` 获得 `0x8000`，上游 Moonlight 固定用相同 `SDL_USEREVENT` 发送 FRAME_READY。新增内部停止分支在上游事件分派前匹配这个编号，错误地将首帧通知当作停止。父代理以真实SDL库、全新进程执行SDL_Init(EVENTS)和注册，确认返回0x8000并冲突。此前17项离线测试未覆盖首帧事件分派，不能证明真实播放通过。

旧包停用。修复已移除内部SDL事件，改为原子停止标记，由20ms主循环处理；保留上游视频事件原有分派。服务端无需回退或改变源模式。

修复包：`smb://192.168.123.10/zssd/Target/RKMoon-Windows-x64-auto-display-exitfix-b49e86cccbc3.zip`，29,320,927 bytes，SHA256 `e9b6e076f0d19dfd195076b58798cd8c2568eb2124edc3b77f60298ffc5a6f5d`。父/子代理均完整读回，sidecar一致。Windows Release、冷进程SDL回归、Qt18/18、gain及clean-PATH4秒启动通过；Mac18/18。实际用户串流待修复包复测。VM9006/200已停机，临时SSH与控制私钥清理，cleanup guard通过，期限2026-09-25 11:30:41 CST。
