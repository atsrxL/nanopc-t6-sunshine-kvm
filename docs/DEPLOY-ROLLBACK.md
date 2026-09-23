# 独立启动、停止与回滚

**前提：全量构建门与P1已通过，用户已明确授予采集窗口。** 本包没有安装/启用任何服务。不要在旧程序正在采集时抢开节点。

## 私有配置

复制`config/example.json`到仓库外的0700目录，chmod600，改成实际绝对路径。`capture_device`必须来自只读基线。默认所有权未授权、输入关闭、像素拷贝关闭、高分辨率关闭。

video-only阶段设置`capture_ownership_authorized: true`，必要时明确设置`allow_cpu_pixel_copy: true`；输入仍为false。不改变源HDMI显示模式。1080p对齐限制详见README。

`state_directory`必须独立、属运行用户、0700。监督器为它创建私有HOME/XDG和app/config，旧文件不会覆盖。配对密钥/登录状态全部留在这里，不进仓库。

```bash
python3 tools/run.py prepare --config /absolute/private/rkmoon.json
python3 tools/run.py start --config /absolute/private/rkmoon.json
# 第二终端；只向记录并校验过的本实例 supervisor pidfd 发SIGTERM：
python3 tools/run.py stop --config /absolute/private/rkmoon.json
```

start前台运行，Ctrl+C也可停止。root、setuid/filecap、普通Sunshine二进制、未授权采集、已占用标准端口会被拒绝。该工具不负责证明旧capture已空闲，必须人工确认独占窗口；本地flock仅防本包实例相互争抢。

## 原版Moonlight配对

1. 用独立运行用户启动专用Sunshine；检查本实例`sunshine.log`有`RKMOON_DEDICATED_BUILD_v1`和硬件探测结果。没有MPP能力就停止，不改成软件。
2. 默认管理UI仅本机：在T6本地访问`https://localhost:47990`，或经已经授权的SSH端口转发访问。创建专用管理员凭据，不关闭认证。证书/密码不提交。
3. 原版Moonlight在有线LAN添加T6地址，发起配对；在本实例管理UI输入客户端PIN。选择“HDMI”应用、1920×1080、60fps、SDR；HEVC硬解能力以目标客户端实际统计/日志为准。
4. 分别明确选择HEVC与H264测试。服务端Main8声明或“HEVC=1”日志不能代替客户端硬解证据。不兼容客户端应明确改H264并重连，不在同一会话里偷偷换codec。

上游配置/端口细节如有变动以固定源码为准。构建期保留配对、认证、RTSP/输入加密，不默认公网、不启用UPnP。首次默认标准TCP47984/47989/47990/48010、UDP47998/47999/48000需要空闲；它们是上游常用端口，实际监听以本地`ss`复核，其他监听失败同样不得停止占用者“腾位”。不自动开防火墙。

## P3输入

视频通过后检查现有kvmd `mouse.absolute=false`。为私有配置设置`input.enabled=true`、`exclusive_hid_authorized=true`、实际kvmd_socket及可选外部0600auth_headers_file。此时放弃旧VNC/网页输入的同时使用，避免两个控制源冲突。

auth_headers_file是本机生成的JSON字典，仅允许`X-KVMD-User`、`X-KVMD-Passwd`或`Authorization`，不提供密码样例/默认密码。接手代理不应回显其内容。

输入进程启动先neutralize支持的键/按钮，再开租约。这会影响目标电脑状态，所以必须独占授权。发现绝对鼠标就拒绝，不自动改kvmd params/gadget。当前接口是相对鼠标；绝对鼠标/Unicode粘贴/JIS特有键不在验收范围。

## 可选systemd

`systemd/rkmoon.service.example`是用户单元模板，**无[Install]节，不默认enable**。只有用户同意后复制到用户unit目录，替换PROJECT_ROOT/PRIVATE_CONFIG、检查exec路径，然后`systemctl --user daemon-reload`。

```bash
systemctl --user start rkmoon.service
systemctl --user stop rkmoon.service
```

KillMode=control-group，ExecStopPost独立尝试release-all，Restart=no。不要擅自enable linger或系统开机服务。若需要开机运行，另行批准运行账户、ACL和所有权切换策略。

## 停止和回滚

停止本实例→等待Sunshine/worker退出→确认HID日志没有“release incomplete”，必要时在仍拥有独占权时执行`tools/run.py release-all --config ...`→确认新程序不再占用video/MPP→才按原用户方式恢复旧采集服务。工具不会自动恢复/启动一个它没有停止过的服务。

没有进行任何内核/EDID/gadget改动，因此默认无需恢复这些全局状态。源码补丁只在新vendor/sunshine；回滚构建可保留证据后删除独立checkout重新下载，**不要git reset旧仓库**。

用户单元仅在实际复制过时删除那个单元并daemon-reload；不得删除旧KVM单元。独立state包含私钥，保留则可以复用配对；删除需用户确认，会丢配对与凭据。删除state不是停止进程的方法。

SIGKILL/PID消失、kvmd不可达、USB断开都不能证明目标电脑已释放。释放未确认时不要重新授予输入或宣称“回滚完成”。
