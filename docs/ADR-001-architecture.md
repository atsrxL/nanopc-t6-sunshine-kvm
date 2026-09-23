# ADR-001：独立 MPP worker + 专用 Sunshine 编码流适配

日期：2026-09-23。状态：**源码层决策已接受；完整编译/硬件验证待完成**。源码来源见 [SOURCES.md](SOURCES.md)，不可把下述分析写成产品已有能力。

## 三个问题的回答

### 1. 采用哪个版本/分支

采用官方 LizardByte/Sunshine `v2026.914.233613` / `63d35f702ee9e362e43263742981836ec0710384`。该发布含 Linux 高危漏洞修复 GHSA-fp6g-27w5-489j，并已经采用新的 libvirtualhid 输入实现。选择官方固定版本便于跟踪安全更新及维护小型自有补丁，不等于声称这个版本“原生支持 HDMI/MPP”。[S1]

本次有限搜索没有验证到一个同时具备持续维护、现有HDMI+MPP HEVC入口、完整恢复控制且可直接采用的 Sunshine RK3588 分支。不能据此推断世界上没有这样的分支。

Wolf 作为替代实现提供 headless/GStreamer 和 Moonlight 编码/发送集成，但本项目未验证它的 RK3588 HDMI/MPP 实机路径；引入新的平台与传输集成工作并不能自动减少当前交付风险。因此保留为后续评估对象，不复制它的协议代码。[S8]

### 2. 哪条改动更集中

选择 B。不是 Sunshine 原有“external_stream=socket”选项，而是新增一个受控源码入口：每次会话启动私有 worker，读完整 AU 后包装为真实 `video::packet_raw_generic`，交回上游发送。反向用私有 IPC 传递 IDR / ACK / STOP / 降码率原语。

A 需要进入捕获 display/img/hardware-frame、编码器注册/能力探测、FFmpeg或原生encoder层和格式生命周期。优点是无AU IPC；缺点是MPP/头less/格式约束与上游平台抽象耦合更深。B 仍要处理探测、packet元数据、会话独占及输入，但把驱动buffer所有权集中在一个短生命周期进程，更方便独立P1和崩溃清理。

本包 B 的变更面是 **7 个上游文件 + 新辅助代码**。这是实际补丁生成器的设计范围，不是已经全量编译证明的“全局最小补丁”。A 未实现、未测量 diff 行数，因此不提供虚构的行数/工时对比。

### 3. 输入如何桥接并释放

保留 `src/input.cpp` 的上游解密后输入处理和键状态整理，平台层仅改发送目标。专用版本的 `platf::virtualhid::create_runtime()` 返回空，不创建 T6 本机虚拟设备。`platf::keyboard_update/move_mouse/button_mouse/scroll/hscroll` 投递到非阻塞 C++ 输入客户端，经过同UID私有UDS交给Python租约服务，再调用已有kvmdAPI。[S4/S7]

租约建立前验证 kvmd keyboard/mouse online、`mouse.absolute=false`，不调用模式切换或gadget配置。EOF、心跳失效、队列溢出、协议/后端错误均撤销租约，丢弃残余事件并释放可能已按下的键/按钮；失败则锁住接管权重试。停止后有独立的neutralize恢复入口。这个机制不能让不可达的USB主机“收到”任何数据；物理故障必须在验收中单列失败。

## 对应的真实上游接口

| 层 | 固定源码位置/符号 | 本包做法 |
|---|---|---|
| 视频会话 | `src/video.cpp::video::capture(safe::mail_t, config_t, void*)` | 在chosen_encoder解析前接管，避免无桌面时解引用空指针 |
| 编码器探测 | `src/video.cpp::video::probe_encoders()` | 专用worker实际MPP黑帧编码探测，仅声明可用codec；不走软件/桌面探测 |
| 包抽象 | `src/video.h::packet_raw_t`, `packet_raw_generic`, `packet_t` | 提供完整AU、IDR、连续frame_index、channel_data、frame_timestamp |
| 恢复 | video capture的`mail::idr`事件 | 转发MPP强制IDR；不宣称ref-frame-invalidation，声明false |
| 有界队列 | `src/thread_safe.h::safe::queue_t<T>` | 新增锁保护的rkmoon_try_raise/size/running；限2，不用默认溢出清空策略 |
| 网络会话 | `src/rtsp.cpp::cmd_announce()` / `stream::session::alloc/start` | 认证/加密校验后、输入分配前拒绝第二客户端及不支持模式 |
| 封包/发送 | `src/stream.cpp` 与现有video_packets消费者 | 不重写RTP/FEC/加密；真实发送延迟/清理必须P2验证 |
| 本机输入工厂 | `src/platform/virtualhid_input.cpp::platf::virtualhid::create_runtime` | 返回空，防止本机uinput/libvirtualhid |
| 输入出口 | 同文件`platf::keyboard_update/move_mouse/button_mouse/scroll/hscroll` | 私有租约USB桥接；忽略首版不支持absolute/text/touch/pen |
| 快捷键 | `src/input.cpp::apply_shortcut` | 专用模式不执行T6本地Sunshine快捷键，继续传给目标主机 |
| 无音频 | `src/audio.cpp::audio::capture` | 等待session shutdown，不采集T6音频；不是“实现静音Opus/HDMI音频” |
| 安全启动 | `src/main.cpp` | 不修改Linux环境清理/权限丢弃逻辑；专用模式拒绝root与filecap |

## 无桌面运行策略

专用Sunshine的`enabled()`为编译期专用路径（返回true），不是一个丢失环境变量后会重新选择桌面/软件编码的可选开关。配置文件、HOME/XDG和配对状态隔离。worker缺失、MPP失败、无HDMI信号时返回错误；可以保留Web UI诊断，但不能发送伪造画面。

本包不承诺上游所有图形依赖可删。构建脚本只关闭已核实的CUDA与tray选项，其他上游依赖先保留，按原有安全初始化运行。是否存在无桌面启动阶段的额外阻碍是P0-build/P2的必测项。

## 生命周期、恢复和安全保留

一个Sunshine session一个worker/socket/序列epoch，EOF即结束。只有一帧硬件输入在途，完整EOI且AU校验成功后归还V4L2buffer；encoder销毁在Capture销毁前。socket只传已编码小数据，raw不经IPC。Sunshine ACK前不取下一raw，drain时只跳过未编码帧。收帧后到上游入队超过200ms则关闭会话，不丢参考帧继续发送。

Sunshine配对、用户认证、会话输入加密、RTSP加密、媒体发送机制由固定上游负责，本包不关认证、不替换密码算法、不注册公网服务。新增本地边界使用私有目录/socket与peerUID，不授予本机任意用户USB控制。

## 最小验证实验及结论条件

实验1：在无T6的Linux上构建核心/运行IPC、AU、输入租约及真实C++/Pythonsocket联合测试；本包已通过，证明本地逻辑，不证明MPP或Moonlight。

实验2：本地下载固定上游，dry-run生成7文件diff，再完整编译专用Sunshine与worker。**尚未执行成功**，必须先于接管设备。

实验3：T6上worker --probe（合成黑帧进入MPP，无HDMI）→真实HDMI P1→原版Moonlight P2。只有三个层次分别通过，才能把架构从“源码决策”提升为“此环境已验证”。
