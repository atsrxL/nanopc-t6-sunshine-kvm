# 2026-09-23 T6 实机阶段

用户已授权独立 ARM64 构建、临时独占采集和可恢复 MPP/DMA heap ACL。USB 输入保持关闭。

环境：NanoPC-T6 LTS，Armbian 26.5.1 trixie，6.1.115-vendor-rk35xx。HDMI 1920×1080、59.9952 Hz、BGR24 单平面，stride 5760、sizeimage 6220800，sRGB/full-range 输入。

| 检查 | 结果 |
|---|---|
| Debian 13 ARM64 构建（VM 内模拟执行） | MPP worker、完整 Sunshine 编译链接通过 |
| ARM64 离线测试 | 25 原生、69 Python/联合通过，含 stdin CLOEXEC 回归 |
| T6 原生 probe | 通过，只读，无 S_FMT/EDID 修改 |
| T6 MPP 合成帧 | H264/HEVC 均通过，codec_mask=3 |
| 真实 HDMI HEVC 60 秒 | 3592 帧，59.9659 fps，全部独立解码/VUI 检查通过 |
| 真实 HDMI H264 60 秒 | 3594 帧，59.9993 fps，全部独立解码/VUI 检查通过 |
| Sunshine 非 root 启动及内部 MPP probe | 通过，独立 HOME/state/私有运行库 |
| 原版 Moonlight 6.1.0 配对、应用列表 | 通过，返回 HDMI |
| Moonlight HEVC 硬解视频 | 超过 11 分钟，VideoToolbox + Metal，超过 4 万帧，队列高水位 1 |
| Moonlight H264 重连硬解 | 通过，VideoToolbox + Metal；短时重连测试，非 10 分钟 H264 长测 |
| 真实 USB 输入 | 未测试，input.enabled=false |

HEVC dequeue→AU 中位 9.503 ms、P95 11.570 ms、最大 33.320 ms；H264 中位 9.477 ms、P95 11.454 ms、最大 29.618 ms。不含 HDMI 前端、网络和客户端显示，不能称端到端延迟。测试源内容未控制，码率和画质不是复杂运动压力验收。

## 本次发现和修复

- 原 BGR CPU 转 NV12 在未缓存 MMAP 上重复读取，仅约 12 fps。普通内存中转约 33 fps；最终使用对齐的 BGR 拷贝缓冲并交 MPP 硬件转换，达到 60 fps。日志仍为 explicit-cpu-pixel-copy，不宣称 DMA-BUF 零拷贝。
- 固定 VEPU580 使用 prep:range 同时选择 RGB→YUV 输出矩阵和编码 VUI。range_out 不作用于此编码路径。修正后两种编码均为 limited-range BT.709、sRGB transfer，与帧元数据一致。独立验证见两个 validation.json。
- Sunshine 子进程 stdin 带 CLOEXEC，MPP 构造函数可能获得 fd 0，而 SDK 将 fd 0 误判为失败。Child 在 exec 前显式打开 /dev/null 为 stdin，修复内部探测并增加回归用例。未关闭上游权限清理或认证。
- 目标板缺少若干上游运行库，复制构建容器中的依赖至独立 runtime-lib，通过实例 LD_LIBRARY_PATH 加载，未安装系统包。

原始视频/日志在 T6 `/home/at/rkmoon-20260923/`，Mac 私有证据在 `private/results/local/`。登录凭据、配对状态和视频不提交。测试窗口开始时旧三个服务已为 inactive（早先只读基线为 active），脚本按每次窗口入口状态恢复，不擅自启动已停服务。ACL 原始快照存放 T6 `/root/agent.backup/`。

客户端 macOS / Apple M4、Moonlight 6.1.0。画面已实际观察；日志报告屏幕 50 Hz，不能据此宣称 60 Hz 显示呈现。HEVC 会话超过 10 分钟无日志中的解码错误，显式终止后 H264 能重新连接。无音频路径未阻止这个客户端连接，不代表所有客户端兼容。

## 窗口结束与 P3 只读检查

视频测试结束后已确认 Sunshine/worker 无残留进程；video0、mpp_service、DMA heap 临时 at 用户 ACL 已撤销。旧 t6-kvmd、t6-kvmd-vnc、t6-kvm-panel 保持窗口入口的 inactive 状态。

P3 只读检查确认 `/etc/t6-kvm/main.yaml` 使用启用鉴权的 `/run/t6-kvm/kvmd.sock`；keyboard 为 `/dev/hidg0`，mouse 为 `/dev/hidg1` 且 `absolute: true`，mouse_alt 未配置。实际 configfs `hid.usb1` report_length=7，X/Y Input 标志为 Absolute，与配置一致。服务未运行，尚不能验证在线 API 鉴权与 HID 状态。未读取或公开密码文件，未发送 USB 事件，未修改 gadget。

建议下一授权窗口：保存原配置、描述符和 UDC 绑定到 `/root/agent.backup/`；将现有鼠标改为匹配 kvmd 的相对模式并重建 gadget（目标电脑 USB 键鼠会短暂重连）；隔离其他输入源后测试 Moonlight 键鼠和异常释放；结束时恢复原绝对模式、绑定和窗口入口服务状态。实施前仍需核对现有 gadget 创建入口，并审阅具体配置差异。此方案尚未执行。
