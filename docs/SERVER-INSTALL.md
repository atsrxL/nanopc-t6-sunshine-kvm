# 服务端安装、更新与回滚

T6 上的服务端以一个自包含的发布目录运行，由 systemd 开机自启。发布目录里有服务端程序、编码 worker、全部私有运行库、HID 桥、开机脚本、EDID 和声卡内核模块，不依赖 home 目录下的任何文件。

## 目录布局

| 路径 | 内容 |
|---|---|
| /opt/rkmoon/releases/VERSION/ | 一个发布：bin（rkmoon-kvm、rkmoon-worker、assets）、lib（Sunshine 依赖库和 librockchip_mpp.so.1）、python/rkmoon_hid、tools、config（EDID）、kernel（rkmoon_hdmirx_codec.ko）、systemd、MANIFEST.json |
| /opt/rkmoon/current | 指向当前发布的符号链接 |
| /etc/rkmoon/runtime.json | 运行配置，首次安装生成，之后安装不覆盖 |
| /var/lib/rkmoon/ | 运行状态和日志（hid.log、sunshine.log、xdg/sunshine/sunshine_state.json），属运行用户，0700 |
| /var/lib/rkmoon-edid/original.bin | 第一次写 EDID 前接收端原有 EDID 的备份 |

## 开机顺序

1. rkmoon-hdmirx-audio：重绑 HDMI-RX 录音 codec（已绑定时不操作）。
2. rkmoon-edid：写入八模式 EDID（config/rkmoon-edid-eight-modes.bin，首选 1080p120），读回逐字节校验；已经一致时不写，不会让被控机重新识别显示器。
3. rkmoon-input：kvmd USB 键鼠后端（由 tools/install_input.py 另行安装）。
4. rkmoon：以普通用户运行 tools/run.py，拉起 HID 桥和 rkmoon-kvm。启动前给该用户授予 /dev/mpp_service 和 /dev/dma_heap/system-uncached 的 ACL；异常退出 5 秒后自动重启，退出后 ExecStopPost 独立释放全部按键。

被控机关机或 USB 未连接时服务端照常启动，画面和声音可用；HID 桥在 USB 键鼠恢复在线并完成按键释放之前拒绝键鼠租约，恢复后客户端自动获得键鼠。

## 打包

在已有构建产物的机器上（产物来源见 docs/BUILD.md）：

    python3 tools/package_server.py \
      --server build/sunshine/rkmoon-kvm --assets build/sunshine/assets \
      --worker build/native/rkmoon-worker --mpp-lib local/mpp/lib/librockchip_mpp.so.0 \
      --runtime-libs DIR_OF_PRIVATE_LIBS --codec-ko rkmoon_hdmirx_codec.ko \
      --licenses DIR_OF_LICENSES --output OUT_DIR [--version NAME]

输出 OUT_DIR/rkmoon-server-VERSION/ 和同名 .tar.gz，MANIFEST.json 记录每个文件的 SHA256。

## 安装和更新

    sudo python3 tools/install_server.py --release rkmoon-server-VERSION.tar.gz --user at --start

脚本会校验 MANIFEST，把发布复制到 /opt/rkmoon/releases/VERSION，切换 current 链接，安装并 enable 三个单元，然后重启 rkmoon。首次安装时写 /etc/rkmoon/runtime.json；已有配置和状态保持不变。从旧的状态目录迁移时加 --migrate-state OLD_STATE，只复制服务器身份（uniqueid），已经绑定过的客户端不用重新添加。

## 回滚

    sudo python3 /opt/rkmoon/current/tools/install_server.py --activate OLD_VERSION --start

只切换符号链接并重启服务，状态和配置不动。

## 日常操作

    systemctl status rkmoon                 # 服务状态
    journalctl -u rkmoon -f                  # 监督进程和编码器日志
    tail -f /var/lib/rkmoon/xdg/sunshine/sunshine.log   # 连接、音频、键鼠日志
    sudo systemctl restart rkmoon            # 重启服务端（自动释放按键）
    sudo python3 /opt/rkmoon/current/tools/edid_apply.py show   # 当前 EDID 的 SHA256

默认密码 kvm，客户端用 IP＋密码连接，端口 47989。

## 在 T6 上的验证（2026-09-24）

- 发布 20260924-r9：rkmoon-kvm 19111b05…，rkmoon-worker 647c109e…，EDID 99f8da37…，codec 模块 3f1ea066…；tar.gz 约 68MB。
- 旧的临时服务 rkmoon-http-live 已停止，停止后 HID 恢复标记不存在（按键已确认释放），再安装新服务。
- 安装后 rkmoon、rkmoon-edid、rkmoon-hdmirx-audio、rkmoon-input 均为 active 且 enabled；rkmoon-edid 报告 EDID 已一致，未重写；MPP 硬件编码探测成功；47989 用 kvm 密码返回原来的 uniqueid，错误密码返回 401。
- 尚未做 T6 整机重启验证。


## 重装 Armbian 后恢复

git 仓库只有源码，不含编译好的二进制和私有运行库（build/ 不提交）。重装后按下面顺序恢复：

1. 刷 Armbian 26.5.1 vendor 内核 6.1.115-vendor-rk35xx 镜像。声卡模块只接受这个内核版本，内核不同时 rkmoon-hdmirx-audio 会拒绝加载，需要按 kernel/hdmirx-codec/README.md 对新内核重新编译模块，并更新 hdmirx_audio_bind.py 里的版本和 SHA。
2. 在 /boot/armbianEnv.txt 的 extraargs 保留 cma=256M，然后重启。
3. 安装系统包：acl，以及 docs/INDEPENDENT-INSTALL.md 第 2 步列出的 python3 包。
4. 安装键鼠后端：python3 tools/fetch_sources.py 拉取固定版本的 kvmd，然后 sudo python3 tools/install_input.py --source "$PWD/vendor/kvmd" --user at --udc fc000000.usb，再 sudo systemctl enable --now rkmoon-input。
5. 安装服务端：sudo python3 tools/install_server.py --release rkmoon-server-20260924-r9.tar.gz --user at --start。发布包需要事先备份，或按 docs/BUILD.md 重新编译后用 package_server.py 生成。
6. 服务器身份（uniqueid）会重新生成。如需保留，恢复前备份 /var/lib/rkmoon/xdg/sunshine/sunshine_state.json，之后用 --migrate-state 导入；否则在客户端删除这台主机，再重新添加。

