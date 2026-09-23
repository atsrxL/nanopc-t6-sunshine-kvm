# 最小 Moonlight KVM 服务方向

用户澄清：只希望复用 Sunshine 的传输底层和输入能力，不以完整 Sunshine 服务为交付目标。

旧 `main` 构建的是完整 Sunshine，替换视频/输入平台入口；它已证明 HDMI/MPP 与官方客户端兼容，但不是精简服务。`feat/minimal-kvm-server` 现抽取专用 `rkmoon-kvm` 入口与独立 CMake 输出名（源码级改动，构建/运行证明单独记录）。Web UI 目标/依赖/npm、Web 管理监听、原桌面主入口、UPnP 与 mDNS 启动、原 apps.json 应用清单加载均从专用运行路径移除；固定 HDMI 应用为唯一占位，不启动外部命令。无网页 PIN 配对使用仅本地同 UID 的 0600 Unix 管理 socket 与 `tools/admin.py` 隐式终端 PIN；GameStream HTTP/HTTPS（不是网页）、RTSP 与流 UDP 仍必须监听。

保留官方 Moonlight 必需的主机信息、首次配对/证书验证、固定 HDMI 应用返回、会话启动/停止、RTSP、加密控制与输入、RTP/FEC/IDR、独占与异常释放。传输底层单独不足以让原版客户端连接。

当前抽取边界：构建仍包含固定 Sunshine 的 `nvhttp`/`rtsp`/`stream`/`crypto`/`input`/`audio`/`video`/`httpcommon`/`process`/`display_device` 和 Linux common/部分图形、虚拟 HID 适配库；`process` 仅为固定会话生命周期与上游协议兼容，配置中的命令列表忽略，不是重新实现游戏管理。上游图形/平台模块部分仍有编译依赖，下一阶段再逐个裁剪，**不能宣称已经获得只有 RTP 库的独立极小二进制**。构建关闭 DRM/X11/Wayland/Portal/KWin/VAAPI/Vulkan/CUDA 捕获和软件编码路径；HDMI 仍只由 MPP worker 打开，不加入 CPU 视频编码兜底。

音频决策：被控机 HDMI 音频在 T6 的明确指定 ALSA `hw:`/`plughw:` 录音设备→48 kHz、双声道、S16_LE→Opus 低延迟（5/10/20ms，标准 stereo 96 kbps / 上游 HIGH_QUALITY stereo 512 kbps、CBR）→Sunshine audio RTP/FEC sender。`hw:` 请求真实设备格式；**`plughw:` 由管理员明确选择，ALSA 可能做格式/采样率转换**，不能宣称原生48k。专用 audio_capture 在 `src/audio.cpp` 原“无音频等待”入口调用，不启动 PulseAudio 录制/静音 sink，亦不提供麦克风回传。必须明确指定源，不能隐式采集 T6 本机默认麦克风；初次无源与运行中丢失源时仍按包长发静音、每 2 秒重试，视频/输入继续。按协商包长请求 ALSA capture period，限制实际缓冲≤100ms、积压超过 max(两个包, 实际设备 period+一个包) 时 drop/prepare 清空但不强制重开并发静音、xrun 重开，不回放过时 PCM；具体设备支持、负载和静音恢复需硬件验证。发送器已有 RTP 序号及每包 packetDuration 时间戳；当前 **未实现 HDMI 硬件时间戳与视频 PTS 的共同时基**，同步精度须以实机量测而非只看 Opus 成功判定。

本阶段特意保留成熟 Sunshine 网络协议，因此定制客户端可先复用 GameStream；不强求继续与官方客户端兼容（变更时需双方协商）。输入只走原加密控制与隔离的 USB HID 租约，不给服务器 root/setcap。

构建、模拟、真实 ALSA/HDMI 采集、端到端口型/画面同步与新客户端结果分别在验收表记录，不能以单项替代。继续审计剩余固定 Sunshine 编译依赖及许可，保留认证/权限清理；再裁剪 `process`/display/平台依赖须单独完成构建及安全回归。
