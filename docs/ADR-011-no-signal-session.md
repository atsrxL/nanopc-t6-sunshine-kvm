# ADR-011: 无信号时仍可建立会话（黑屏占位＋键鼠）

状态：已采纳（2026-09-25）

## 背景

被控机休眠或关屏时，HDMI-IN 没有信号。旧流程里，客户端要等到 `RKMoonDisplayStatus=ready` 才发起串流；服务端 worker 打开采集时也会因 QUERY_DV_TIMINGS 失败而退出。结果是用户无法通过键盘鼠标唤醒被控机。

## 决定

- 客户端点击 Connect 后只获取并持续刷新服务端信息（主机名、ID、HDMI 状态与模式、鼠标模式、编码），不自动打开串流。串流只在用户点击 **Start** 后开始。
- `no_signal` 状态下 Start 仍然可用：客户端按 1920×1080@60 请求会话。
- Sunshine bridge 在 capture 开始时读取 `current_display()`。如果状态是 `no_signal`，就以 `--no-signal-placeholder` 启动 worker。
- 占位模式下，worker 不打开 HDMI 设备，而是用 MPP 硬件编码合成黑帧，每秒 10 帧，协议与 ACK 流控保持不变。HID 租约照常由 `maintain_input` 维持。
- 信号出现后，客户端的模式轮询会看到 `ready`；连续两次都看到时，按现有模式切换流程重启会话，进入真实模式。
- `unsupported` 和 `unavailable` 两种状态仍然不能启动会话。

## 约束说明

占位帧由 MPP 硬件编码器编码，与 `--probe` 使用的是同一条合成黑帧路径。它不属于 CPU 视频编码，也不属于桌面重抓屏兜底；只要有真实信号，就不会用占位帧替代。

