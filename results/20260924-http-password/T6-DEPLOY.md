# T6 HTTP密码版本部署

2026-09-24用户报告客户端拒绝host auth marker。实测旧rkmoon-minimal-live运行912613c，serverinfo没有RKMoonAuth，HttpsPort47984/PairStatus0，确认为新客户端连接旧配对版服务端。

独立部署到/home/at/rkmoon-http-1440p90，server SHA256 de8617a7626aa95978a49e2fdbd8460471a837202a94c688f930a391175ec111，worker9158a60d319e28b818e900c5d7bfc7336a2109cddb607091023944f824269a3e；复用旧私有运行库及已验证固定MPP。备份旧unit/runtime到/root/agent.backup/rkmoon-password-deploy-20260924。停止旧实例后其HID recovery marker消失，才启动新实例，未覆盖旧树。

首次新启动发现USB keyboard.online=false/mouse.online=false，HID bridge检查拒绝。因此新runtime明确input.enabled=false，先恢复密码登录/视频服务；USB输入目前不可用，不能宣称已修好。外部rkmoon-input服务未改，未改gadget/USB角色。音频沿用原配置，但未实际听音验证。

rkmoon-http-live为at普通用户的临时systemd unit，active；没有安装开机持久服务。运行配置允许high_resolution及1440p90实验，但没有切HDMI源/EDID，输入保持1080p60。

从Mac直连T6 LAN验证：正确密码serverinfo XML200、RKMoonAuth=password-http-v1、HttpsPort0；错误密码XML401；正确密码applist XML200。此验证无视频流。客户端应使用192.168.123.99:47989，密码kvm，当前视频先选1080p60。旧绑定若因新state UUID变化被拒绝，应Forget binding后重新连接。
