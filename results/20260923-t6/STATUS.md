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
| Moonlight 硬解视频/10 分钟 | 待完成 |
| 真实 USB 输入 | 未测试，input.enabled=false |

HEVC dequeue→AU 中位 9.503 ms、P95 11.570 ms、最大 33.320 ms；H264 中位 9.477 ms、P95 11.454 ms、最大 29.618 ms。不含 HDMI 前端、网络和客户端显示，不能称端到端延迟。测试源内容未控制，码率和画质不是复杂运动压力验收。

## 本次发现和修复

- 原 BGR CPU 转 NV12 在未缓存 MMAP 上重复读取，仅约 12 fps。普通内存中转约 33 fps；最终使用对齐的 BGR 拷贝缓冲并交 MPP 硬件转换，达到 60 fps。日志仍为 explicit-cpu-pixel-copy，不宣称 DMA-BUF 零拷贝。
- 固定 VEPU580 使用 prep:range 同时选择 RGB→YUV 输出矩阵和编码 VUI。range_out 不作用于此编码路径。修正后两种编码均为 limited-range BT.709、sRGB transfer，与帧元数据一致。独立验证见两个 validation.json。
- Sunshine 子进程 stdin 带 CLOEXEC，MPP 构造函数可能获得 fd 0，而 SDK 将 fd 0 误判为失败。Child 在 exec 前显式打开 /dev/null 为 stdin，修复内部探测并增加回归用例。未关闭上游权限清理或认证。
- 目标板缺少若干上游运行库，复制构建容器中的依赖至独立 runtime-lib，通过实例 LD_LIBRARY_PATH 加载，未安装系统包。

原始视频/日志在 T6 `/home/at/rkmoon-20260923/`，Mac 私有证据在 `private/results/local/`。登录凭据、配对状态和视频不提交。测试窗口开始时旧三个服务已为 inactive（早先只读基线为 active），脚本按每次窗口入口状态恢复，不擅自启动已停服务。ACL 原始快照存放 T6 `/root/agent.backup/`。
