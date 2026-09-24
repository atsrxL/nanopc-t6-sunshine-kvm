# ADR-006: 限定 2560×1440 ~90Hz 实验

2026-09-24。90Hz 仅作为显式实验模式；常规约 60Hz（fps_x100 5900..6010）保持原有范围。90Hz 仅接受宽 2560、高 1440、fps_x100 8900..9010，其他尺寸和频率拒绝。独立 worker 必须提供 --allow-1440p90-experiment；Sunshine 必须同时启用运行配置 allow_1440p90_experiment: true 和既有 allow_high_resolution: true，前者经 RKMOON_ALLOW_1440P90_EXPERIMENT=1 传给 bridge，bridge 再传 worker CLI。缺省关闭，字符串值拒绝。

90Hz 会话使用 GOP 90，60Hz 保持 GOP 60。MPP level 按真实 fps 计算：1440p90 HEVC Main tier 为 L5.1，H.264 Baseline 为 L5.2；1440p60 分别为 L5 与 L5.1。无 CPU 视频编码兜底。硬件采集必须保留 G_FMT 和每帧长度校验，不主动 S_FMT。

这次变更只用于吞吐与兼容性验证，不代表持续 MPP 编码、码流独立解码、GameStream 传输或客户端 90fps 验收。固定 MPP/Sunshine commit 未升级。
