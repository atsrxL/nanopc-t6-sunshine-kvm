# 测试说明与证据等级

## E0：离线纯逻辑

`tools/build.sh offline`运行原生25项及Python/联合工具68项。ASan/UBSan另行运行。测试fake kvmd、短NAL字节夹具、模拟source anchors都是测试数据，不是生产路径。`rkmoon-hid-fixture`不会被链接/安装到worker。

测试覆盖：64字节头与边界、短包/EOF/deadline、Child fd继承、H264/HEVC IDR/参数集/单picture、序号、NV12/BGR像素与stride、相对输入映射/滚轮/别名、两个有界队列、租约和release retry、真正C++→Python UDS联合行为、配置安全门、补丁锚点精确性。

未覆盖：真实MPP编译/运行、驱动DMA coherency、USB实际送达、完整上游构建、Moonlight解码、网络FEC/拥塞、画质和物理时延。测试fixture不应作为生产“临时替代后端”。

## E1：完整构建

固定MPP+固定Sunshine+自有overlay全部编译链接，保存工具链/SDK版本与binary/library SHA256。 `ldd`确认未误用旧系统MPP；记录Sunshine子模块revision。对于完整上游源文件变更生成实际diff并检查`nvhttp`/crypto认证逻辑没有被绕过。

## E2：MPP能力与真实HDMI

先`worker --probe`：合成NV12黑帧经过真实MPP，确认H264/HEVC能力。该能力探测不读取HDMI，不证明source layout正确。

再分别真实输入HEVC/H264各60秒。用p1_capture.py保存码流、frames.csv、resources.jsonl、worker.log、command.json、exit.json。脚本不执行播放器/桌面捕获。原生尺寸/fps不匹配时失败，不能自动改EDID。

用validate_capture.py或另一独立ffmpeg完整解码，检查codec、Main/Baseline profile、level、8-bit420、无B frame、尺寸、BT709、源range/transfer一致、帧数/序号/时戳。验证器没有源颜色期望值时会明确把对应一致性列为未核实，不给P1硬件验收盖章。

IDR实验使用GOP120+1秒强制请求，检查约第60帧额外IDR与inline参数集；开关/重连三次后能重新取得采集节点。CPU像素路径必须记录，不要求为了测试而改源模式；验证静态文字、滚动、运动、纯色/灰阶、亮暗阶、边缘位移。

## E3：原版客户端

P2先video-only、无HDR、实际1080p60。保存客户端系统/version/GPU/decoder/刷新率、硬解状态、接收/解码/render统计，不只保存服务端fps。10分钟内采样CPU/温度/内存、错误/队列高水位/延迟是否持续增长。

分别协商HEVC与H264；不支持HEVC时明确报错或重连H264。验证无网页的同UID Unix PIN CLI、错误PIN、碎片socket读写、正常退出、立即重连、恢复关键帧。最小服务音频需另测显式 HDMI ALSA `hw:` 设备真正来自被控机、5/10/20ms 48k stereo Opus/RTP、96kbps/高质量512kbps、静音期间 RTP 连续性、源首次缺失/后接入、运行中丢失/重接、xrun与调度积压后的音画同步；ALSA `null`/stub 只算离线能力探测，不能冒充 HDMI/客户端声音。测试第二客户端拒绝且第一会话不受影响。HDMI拔插/无信号/同尺寸format/color变化/分辨率变化应干净结束，不能继续虚报旧尺寸。

P3在用户授权且mouse.absolute=false后进行：在被控电脑上看到真实键鼠效果，验证修饰键/组合键、按住、双击、滚轮、断开后release。确认T6本地没收到uinput事件。kill/断网测试作用域限定本实例。

## E4：时延与稳定性

CSV的dequeue→AU和Sunshine dequeue→enqueue不是完整HDMI capture latency。服务器分阶段用同机单调时钟；客户端跨机时间不能直接相减。显示→显示用同框/高速摄影，输入→显示另列；报告采样数、p50/p95/p99、源刷新/扫描显示/客户端缓冲、码率、画质内容。

旧方案、新HEVC、新H264必须同源电脑、同分辨率网络客户端负载对照；旧70.47ms /13.68ms仅作参考，不作为本次结果。

原生1440p60/4K60分开验收每一层，随后2h/8h。限带宽/丢包只在授权隔离环境；关注画面年龄、重连/恢复次数、内存温度、延迟漂移，不以“服务端60fps”替代客户端可用。
