# 安全与授权边界

本包面向用户自有/获准的本地KVM。默认LAN、上游配对/身份认证保留，管理UI仅本机，不做公网暴露/UPnP、不关闭加密。固定版本包含已发布的Linux安全修复，不意味着本包及全部依赖已安全审计；投入公网前必须重新审查上游公告与新补丁。[S1]

## 信任边界

原版Moonlight与Sunshine之间使用固定上游协议实现。本包新增IPC只在父子进程socketpair和私有同UID socket内部；本机同UID可修改配置/二进制，属于受信任执行主体，而不是密码学隔离。

Sunshine binary必须专用marker、非root执行、非setuid、无file capabilities、非group/world writable。原有Linux环境净化/权限丢弃未改动。设备访问由经授权的最小ACL/组提供，不给CAP_SYS_ADMIN、不chmod666全部设备。worker不需要网络，不运行外部shell，不修改系统配置。

输入API使用既有kvmd授权机制，外部0600文件可存必要headers，绝不写入仓库。socket路径不是认证替代，peerUID加目录权限限制本地边界。不要共享运行账号给不可信应用。

## 安全默认

配置capture_ownership_authorized=false、input.enabled=false、exclusive_hid_authorized=false。启动失败不停止占用者、不改源显示模式，不走软件编码/桌面fallback。无信号/变化/超时终止，不伪造画面骗客户端。

畸形IPC长度/版本/序号、超大AU、过期帧、输入队列饱和均fail-closed。修复兼容性不得删除这些边界。ERROR诊断不回传键内容/密钥。

## 进程和设备故障

受控子进程parent-death SIGTERM，worker独占硬件；独立输入服务根据EOF/heartbeat回收。systemd模板KillMode=control-group，ExecStopPost尝试neutralize。SIGKILL或硬件故障下清理是best effort，不能承诺所有外设即时释放；失败需保持所有权锁定/人工确认。

对于kvmd/其他UI同时写HID，本包不能强制跨程序独占。用户必须安排输入窗口，避免旧网页/VNC控制同时操作。不要未经授权终止kvmd来制造独占——本包恰好依赖它。

## 日志/交付

只保存构建、计数、时戳、帧大小、温度/内存等。源码包不带任何真实按键记录、密码/配对材料、客户IP/MAC/主机序列号。只读probe输出仍应审阅后再分享。真实码流可能包含源电脑画面中的敏感内容，不能默认提交；提交hash和脱敏图像/统计即可。

不处理HDCP绕过。受保护HDMI不能采集时明确报错。网络损伤/限速只在单独授权的隔离测试环境，不更改用户现网来“证明低延迟”。
