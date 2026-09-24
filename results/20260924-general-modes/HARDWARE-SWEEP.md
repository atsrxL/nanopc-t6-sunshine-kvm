# 通用源模式 T6 实机采集/编码扫测

2026-09-24。源：Znas Tiny11_clone RTX5060Ti 按最终8模式EDID切换输出（Windows枚举模式，无NVAPI自定义）。
T6 独立 worker，25Mbps CBR，每档10秒（1440p120 HEVC 另有30秒），--allow-copy，mid IDR。
码流全部在 Mac 上用 ffmpeg/ffprobe 独立完整解码（tools/validate_capture.py）。
这些是真实HDMI采集+MPP硬编+独立解码证据，不是 Moonlight 客户端播放验收。

| 模式 | 编码 | 路径 | 帧数 | 实测fps | raw_skipped | dequeue→AU p50/p99 ms | 结果 |
|---|---|---|---|---|---|---|---|
| 2560×1440@119.99 | HEVC 30s | DMA-BUF | 3594 | 120.0 | 0 | 6.8 / 7.1 | 通过 |
| 2560×1440@119.99 | H.264 | DMA-BUF | 1194 | 120.0 | 0 | 6.5 / 6.8 | 通过 |
| 2560×1600@119.95 | HEVC | DMA-BUF | 1192 | 119.8 | 2 | 7.5 / 7.8 | 通过 |
| 1920×1280@89.96 | HEVC | DMA-BUF | 894 | 90.0 | 0 | 4.7 / 5.0 | 通过 |
| 3840×2160@60 | HEVC | DMA-BUF | 595 | 60.0 | 0 | 14.9 / 15.6 | 通过 |
| 3840×2160@30 | HEVC | DMA-BUF | 294 | 30.0 | 0 | 15.0 / 16.4 | 通过 |
| 1920×1080@60 | HEVC | CPU 拷贝 | 592 | 59.8 | 2 | 9.3 / 12.3 | 通过 |
| 1920×1080@30 | HEVC | CPU 拷贝 | 295 | 30.0 | 0 | 9.3 / 37.1 | 通过 |
| 1920×1080@119.99 | HEVC（worker be9c807） | CPU 拷贝 | 1022 | 102.7 | 170 | 9.4 / 12.0 | 未通过（掉帧约14%） |
| 1920×1080@119.99 | HEVC（worker 18164ea） | CPU 拷贝 | 1080 | 108.5 | 114 | 8.9 / 11.4 | 未通过（掉帧约10%） |

1080 行高不是16的倍数，MPP 不能直接导入采集 DMA-BUF，只能显式像素拷贝到对齐缓冲。
worker 18164ea 去掉了每帧整缓冲 memset（只清对齐填充），提升约6fps，但 1080p120 仍超出拷贝路径能力。
需要满帧120Hz时用 2560×1440@120 或 2560×1600@120。

H.264 1440p120 首次校验失败，原因是 ffmpeg 对无时间戳 Annex-B 裸流在高帧率下报 DTS 警告，解码本身无错误；
tools/validate_capture.py 现给解复用器提供标称帧率后通过。

部署：T6 /home/at/rkmoon-custom-modes/，rkmoon-kvm b3819478f92b793b1df21279b0885ec83c11b87417a4a49be8cbe2b0ced53796，
worker 18164eaa967418d263169038e205fda8a2ae644cba1b6d6704189e33dafc87b2（旧 be9c807 保留为 worker.be9c807）。
runtime.json 备份于 T6 /root/agent.backup/rkmoon-custom-server-20260924/。部署后 serverinfo（密码 kvm）
分别正确广播 2560×1440/11999 与 1920×1080/11999，鼠标模式 relative,absolute。
