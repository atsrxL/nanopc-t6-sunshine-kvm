# 相对触控板与绝对鼠标

用户定义及约束见 [ADR-008](../../docs/ADR-008-mouse-modes.md)。此前只支持相对USB，Ctrl+Alt+Shift+C显示本地指针后仍发送delta，不能保证本地/被控指针对齐。新实现将输入模式与指针显示分离。

## T6 / Znas 硬件验证

- 原后端只有relative hidg1。启用固定kvmd原生mouse_alt，将主鼠标保持relative、alt为absolute。runtime解析实际minor及设备权限。
- 首次第三HID创建失败ENODEV；旧未绑定t6-kvm占用minor2/3，加上当前0/1已耗尽名额。停止推进并恢复原输入/video成功。
- 确认旧gadget未绑定UDC、旧kvmd/VNC/panel inactive后，备份其两个HID描述符和链接到 `/root/agent.backup/rkmoon-dual-mouse-20260924/unused-old-hid.json`，只释放闲置function，保留旧仓库和gadget其余结构。
- 再次创建成功：keyboard hidg0、relative hidg1、absolute hidg2；UDC configured，kvmd两个outputs usb/usb_rel available，键鼠online。配置/runtime原件备份在同一目录。
- Znas USB重枚举从bus8device2变为device4，原live直通仍指旧地址。备份live/persistent XML到Znas `/root/agent.backup/rkmoon-dual-usb-20260924/`，仅detach/attach同一RKMOON001 USB，按vendor/product解析，未修改持久VM配置。Windows QGA确认MI00键盘、MI01/MI02两个鼠标均OK。
- 在停止video并确认HID租约释放后，只发送合成鼠标坐标，无点击/按键。Windows交互会话GetCursorPos、DPI-aware读回：发送(-16384,+16384)得到(639,1079)，发送(0,0)得到(1279,719)，屏幕2560×1440，符合25%/75%与中心位置（约1px舍入）。这是实际USB主机消费证据，非客户端全链路。
- 测试任务/脚本已删除；恢复relative输出和原video，等待新server/client编译部署。

## 软件阶段

运行配置新增allow_absolute_mouse，默认false；只有input启用且显式允许时发布RKMoonMouseModes=relative,absolute。本地配置gate16/16，双HID配置2/2通过。

Windows客户端已构建交付：`RKMoon-Windows-x64-mouse-modes-c6b7b7db715c.zip`，29,324,997bytes，SHA256 `e6fe614dc07c7956764855118253c547290298ae2551a610de4be3e11baa6f53`，Target父/子代理完整读回一致。Windows/Mac Qt22/22、冷启动SDL、gain和clean-PATH启动检查通过；VM9006/VM200 stopped，临时SSH清理，guard通过，期限2026-09-25 12:16:14 CST。

服务端父代理补齐launch/resume rkmoonMouseMode传递、RTSP config字段，字段追加到结构尾以保留上游聚合初始化。禁止上游100ms桌面唤醒微移，避免绝对会话混入相对包。

## 部署完成

ARM64完整链接通过，31原生及111 Python/联合通过（2跳过）。生产binary在隔离容器中验证能力开关、非法/重复/未授权模式拒绝、HTTP密码、无pair/TLS、重启身份与退出清理通过。

服务端SHA256 `b0fdc631a777038c443c944078ad74bbc543b504ac47dfa86852bae142a6730b`，部署 `/home/at/rkmoon-mouse-modes/rkmoon-kvm`。runtime与输入Python已同步更新，原身份保持；原配置/code备份 `/root/agent.backup/rkmoon-mouse-server-20260924`。启用allow_absolute_mouse。LAN认证字段实测relative,absolute及2560×1440/8999，video/input均active。

远端产物和日志保留VM301 `/root/rkmoon-artifacts/20260924-absolute`；临时构建树、导入镜像、宿主传输包与HTTP导出已清理。VM301保留既有13:16:14 CST优雅关机计时器。

真实Moonlight绝对/相对/非16:9/DPI/快捷键最终验收待用户新包验证；不能用上述直接USB坐标测试替代客户端全链路。快速跨模式重连的旧输入异步队列竞态未做压力验收。

## 13:03 首帧退出复查

用户实测mouse-modes-c6b7b7db715c仍首帧退出。T6四次连接后约0.18–0.21秒均记录 `HID lease lost; terminating session`。本轮定位到固定moonlight-common-c `8599b604` 的 Connection.c 在 `connectionStarted()` 前无条件发送(+1,+1)、(-1,-1)相对微移；绝对租约拒绝模式不符输入。前次只禁用了Sunshine端的微移，遗漏客户端嵌套依赖。正在去掉该客户端微移、强制重编common，并将模式选择移到主GUI。旧包不能标记真实绝对串流通过。

修复已交付：Windows snapshot `0fb281698fd2385854b43e3888ef328c61064df0`，全新build目录重编Connection.c/common及最终应用。Windows/Mac Qt22/22、Python4/4、冷启动SDL3/3、gain/clean-PATH启动通过。鼠标模式位于主GUI。

按照用户新要求，最终仅保留Target/RKMoon-Windows-x64未打包目录；40个payload文件69,670,665bytes，父/子代理逐文件读回匹配delivery-manifest.json。exe SHA256 `1acb2fcde4ee238641c3cd3b35ca34f02bf8b8ac47c434e421c0020c75b940d5`。目录交付校验后，父代理删除旧客户端ZIP与sidecar三项，保留音频驱动和其他工具。实际新程序串流待用户确认。VM9006/200 stopped，租约和私钥清理，guard通过，期限2026-09-25 13:17:43 CST。
